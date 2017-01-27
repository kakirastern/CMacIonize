/*******************************************************************************
 * This file is part of CMacIonize
 * Copyright (C) 2016 Bert Vandenbroucke (bert.vandenbroucke@gmail.com)
 *
 * CMacIonize is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CMacIonize is distributed in the hope that it will be useful,
 * but WITOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with CMacIonize. If not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************/

/**
 * @file CMacIonize.cpp
 *
 * @brief Entrance point of the CMacIonize program
 *
 * @author Bert Vandenbroucke (bv7@st-andrews.ac.uk)
 */
#include "Abundances.hpp"
#include "Box.hpp"
#include "ChargeTransferRates.hpp"
#include "CommandLineOption.hpp"
#include "CommandLineParser.hpp"
#include "CompilerInfo.hpp"
#include "Configuration.hpp"
#include "ContinuousPhotonSourceFactory.hpp"
#include "CoordinateVector.hpp"
#include "DensityFunctionFactory.hpp"
#include "DensityGridFactory.hpp"
#include "DensityGridWriterFactory.hpp"
#include "EmissivityCalculator.hpp"
#include "FileLog.hpp"
#include "IonizationStateCalculator.hpp"
#include "IterationConvergenceCheckerFactory.hpp"
#include "LineCoolingData.hpp"
#include "ParameterFile.hpp"
#include "PhotonNumberConvergenceCheckerFactory.hpp"
#include "PhotonShootJobMarket.hpp"
#include "PhotonSource.hpp"
#include "PhotonSourceDistributionFactory.hpp"
#include "PhotonSourceSpectrumFactory.hpp"
#include "TemperatureCalculator.hpp"
#include "TerminalLog.hpp"
#include "Timer.hpp"
#include "VernerCrossSections.hpp"
#include "VernerRecombinationRates.hpp"
#include "WorkDistributor.hpp"
#include "WorkEnvironment.hpp"
#include <iostream>
#include <string>

#ifdef HAVE_MPI
#include "MPICommunicator.hpp"
#endif

using namespace std;

/**
 * @brief Entrance point of the program
 *
 * @param argc Number of command line arguments passed on to the program.
 * @param argv Array containing the command line arguments.
 * @return Exit code: 0 on success.
 */
int main(int argc, char **argv) {
  bool write_log = true;
  bool write_output = true;
#ifdef HAVE_MPI
  // initialize the MPI communicator and make sure only process 0 writes to the
  // log and output files
  MPICommunicator comm(argc, argv);
  write_log = (comm.get_rank() == 0);
  write_output = (comm.get_rank() == 0);
#endif

  Timer programtimer;

  // first thing we should do: parse the command line arguments
  // we need to define a CommandLineParser object that does this and acts as a
  // dictionary that can be queried
  CommandLineParser parser("CMacIonize");
  parser.add_required_option< string >(
      "params", 'p',
      "Name of the parameter file containing the simulation parameters.");
  parser.add_option("verbose", 'v', "Set the logging level to the lowest "
                                    "possible value to allow more output to be "
                                    "written to the log.",
                    COMMANDLINEOPTION_NOARGUMENT, "false");
  parser.add_option("logfile", 'l', "Output program logs to a file with the "
                                    "given name, instead of to the standard "
                                    "output.",
                    COMMANDLINEOPTION_STRINGARGUMENT, "CMacIonize_run.log");
  parser.add_option("dirty", 'd',
                    "Allow running a dirty code version. This is disabled by "
                    "default, since a dirty code version does not correspond "
                    "to a unique revision number in the code repository, and "
                    "it is therefore impossible to rerun a dirty version with "
                    "exactly the same code afterwards.",
                    COMMANDLINEOPTION_NOARGUMENT, "false");
  parser.add_option("threads", 't', "Number of parallel threads to use.",
                    COMMANDLINEOPTION_INTARGUMENT, "1");
  parser.add_option("dry-run", 'n',
                    "Perform a dry run of the program: this reads the "
                    "parameter file and sets up all the components, but aborts "
                    "before initializing the density grid. This option is "
                    "ideal for checking if a parameter file will work, and to "
                    "check if all input files can be read.",
                    COMMANDLINEOPTION_NOARGUMENT, "false");
  parser.parse_arguments(argc, argv);

  LogLevel loglevel = LOGLEVEL_STATUS;
  if (parser.get_value< bool >("verbose")) {
    loglevel = LOGLEVEL_INFO;
  }
  Log *log = nullptr;
  if (write_log) {
    if (parser.was_found("logfile")) {
      log = new FileLog(parser.get_value< std::string >("logfile"), loglevel);
    } else {
      // ASCII art generated using
      // http://patorjk.com/software/taag/#p=display&h=2&f=Big&t=CMacIonize
      // all '\' have been manualy escaped, so the actual result looks a bit
      // nicer
      std::string header =
          "  _____ __  __            _____            _\n"
          " / ____|  \\/  |          |_   _|          (_)\n"
          "| |    | \\  / | __ _  ___  | |  ___  _ __  _ _______\n"
          "| |    | |\\/| |/ _` |/ __| | | / _ \\| '_ \\| |_  / _ \\\n"
          "| |____| |  | | (_| | (__ _| || (_) | | | | |/ /  __/\n"
          " \\_____|_|  |_|\\__,_|\\___|_____\\___/|_| |_|_/___\\___|\n";
      log = new TerminalLog(loglevel, header);
    }
  }

  if (log) {
    log->write_status("This is CMacIonize, version ",
                      CompilerInfo::get_git_version(), ".");
    log->write_status("Code was compiled on ", CompilerInfo::get_full_date(),
                      " using ", CompilerInfo::get_full_compiler_name(), ".");
    log->write_status("Code was compiled for ", CompilerInfo::get_os_name(),
                      ", ", CompilerInfo::get_kernel_name(), " on ",
                      CompilerInfo::get_hardware_name(), " (",
                      CompilerInfo::get_host_name(), ").");
  }

#ifdef HAVE_MPI
  if (log) {
    if (comm.get_size() > 1) {
      log->write_status("Code is running on ", comm.get_size(), " processes.");
    } else {
      log->write_status("Code is running on a single process.");
    }
  }
#endif

  if (CompilerInfo::is_dirty()) {
    if (log) {
      log->write_warning(
          "This is a dirty code version (meaning some of the "
          "source files have changed since the code was obtained "
          "from the repository).");
    }
    if (!parser.get_value< bool >("dirty")) {
      if (log) {
        log->write_error("Running a dirty code version is disabled by default. "
                         "If you still want to run this version, add the "
                         "\"--dirty\" flag to the run command.");
      }
      cmac_error("Running a dirty code version is disabled by default.");
    } else {
      if (log) {
        log->write_warning("However, dirty running is enabled.");
      }
    }
  }

  // set the maximum number of openmp threads
  WorkEnvironment::set_max_num_threads(parser.get_value< int >("threads"));

  // second: initialize the parameters that are read in from static files
  // these files should be configured by CMake and put in a location that is
  // stored in a CMake configured header
  LineCoolingData line_cooling_data;

  // third: read in the parameters of the run from a parameter file. This file
  // should be read by a ParameterFileParser object that acts as a dictionary
  ParameterFile params(parser.get_value< string >("params"));

  // fourth: construct the density grid. This should be stored in a separate
  // DensityGrid object with geometrical and physical properties
  DensityFunction *density_function =
      DensityFunctionFactory::generate(params, log);
  VernerCrossSections cross_sections;
  VernerRecombinationRates recombination_rates;
  DensityGrid *grid =
      DensityGridFactory::generate(params, *density_function, log);

  // fifth: construct the stellar sources. These should be stored in a
  // separate StellarSources object with geometrical and physical properties.
  PhotonSourceDistribution *sourcedistribution =
      PhotonSourceDistributionFactory::generate(params, log);
  int random_seed = params.get_value< int >("random_seed", 42);
  PhotonSourceSpectrum *spectrum = PhotonSourceSpectrumFactory::generate(
      "photonsourcespectrum", params, log);

  if (sourcedistribution != nullptr && spectrum == nullptr) {
    cmac_error("No spectrum provided for the discrete photon sources!");
  }
  if (sourcedistribution == nullptr && spectrum != nullptr) {
    cmac_warning("Discrete photon source spectrum provided, but no discrete "
                 "photon source distributions. The given spectrum will be "
                 "ignored.");
  }

  ContinuousPhotonSource *continuoussource =
      ContinuousPhotonSourceFactory::generate(params, log);
  PhotonSourceSpectrum *continuousspectrum =
      PhotonSourceSpectrumFactory::generate("continuousphotonsourcespectrum",
                                            params, log);

  if (continuoussource != nullptr && continuousspectrum == nullptr) {
    cmac_error("No spectrum provided for the continuous photon sources!");
  }
  if (continuoussource == nullptr && continuousspectrum != nullptr) {
    cmac_warning("Continuous photon source spectrum provided, but no "
                 "continuous photon source. The given spectrum will be "
                 "ignored.");
  }

  Abundances abundances(params, log);

  PhotonSource source(sourcedistribution, spectrum, continuoussource,
                      continuousspectrum, abundances, cross_sections, log);

  // set up output
  DensityGridWriter *writer =
      DensityGridWriterFactory::generate(params, *grid, log);

  // set up convergence checking
  PhotonNumberConvergenceChecker *convergence_checker =
      PhotonNumberConvergenceCheckerFactory::generate(*grid, params, log);

  unsigned int nloop =
      params.get_value< unsigned int >("max_number_iterations", 10);

  unsigned int numphoton =
      params.get_value< unsigned int >("number of photons", 100);
  double Q = source.get_total_luminosity();

  ChargeTransferRates charge_transfer_rates;

  // used to calculate the ionization state at fixed temperature
  IonizationStateCalculator ionization_state_calculator(
      Q, abundances, recombination_rates, charge_transfer_rates);
  // used to calculate both the ionization state and the temperature
  TemperatureCalculator temperature_calculator(
      Q, abundances, params.get_value< double >("pahfac", 1.),
      line_cooling_data, recombination_rates, charge_transfer_rates);

  bool calculate_temperature =
      params.get_value< bool >("calculate_temperature", true);

  // finally: the actual program loop whereby the density grid is ray traced
  // using photon packets generated by the stellar sources

  IterationConvergenceChecker *itconvergence_checker =
      IterationConvergenceCheckerFactory::generate(*grid, params, log);

  // we are done reading the parameter file
  // now output all parameters (also those for which default values were used)
  // to a reference parameter file
  if (write_output) {
    std::string folder = Utilities::get_absolute_path(
        params.get_value< std::string >("densitygridwriter:folder", "."));
    ofstream pfile(folder + "/parameters-usedvalues.param");
    params.print_contents(pfile);
    pfile.close();
    if (log) {
      log->write_status("Wrote used parameters to ", folder,
                        "/parameters-usedvalues.param.");
    }
  }

  if (parser.get_value< bool >("dry-run")) {
    if (log) {
      log->write_warning("Dry run requested. Program will now halt.");
    }
    return 0.;
  }

// done writing file, now initialize grid
#ifdef HAVE_MPI
  std::pair< unsigned long, unsigned long > block =
      comm.distribute_block(0, grid->get_number_of_cells());
#else
  std::pair< unsigned long, unsigned long > block =
      std::make_pair(0, grid->get_number_of_cells());
#endif
  grid->initialize(block);
#ifdef HAVE_MPI
  // grid->initialize initialized:
  // - densities
  // - temperatures
  // - ionic fractions
  // we have to gather these across all processes
  comm.gather(grid->get_number_density_handle());
  comm.gather(grid->get_temperature_handle());
  for (int i = 0; i < NUMBER_OF_IONNAMES; ++i) {
    IonName ion = static_cast< IonName >(i);
    comm.gather(grid->get_ionic_fraction_handle(ion));
  }
#endif

  // object used to distribute jobs in a shared memory parallel context
  WorkDistributor< PhotonShootJobMarket, PhotonShootJob > workdistributor(
      parser.get_value< int >("threads"));
  const int worksize = workdistributor.get_worksize();
  Timer worktimer;

#ifdef HAVE_MPI
  // make sure every thread on every process has another random seed
  random_seed += comm.get_rank() * worksize;
#endif

  if (log) {
    log->write_status("Program will use ",
                      workdistributor.get_worksize_string(),
                      " for photon shooting.");
  }
  PhotonShootJobMarket photonshootjobs(source, random_seed, *grid, 0, 100,
                                       worksize);

  if (write_output) {
    writer->write(0, params);
  }
  unsigned int loop = 0;
  while (loop < nloop && !itconvergence_checker->is_converged()) {

    if (log) {
      log->write_status("Starting loop ", loop, ".");
    }

    // run the number of photons by the IterationConvergenceChecker to allow for
    // corrections
    numphoton = itconvergence_checker->get_next_number_of_photons(numphoton);

    //    if (loop == 3 || loop == 9) {
    //      numphoton *= 10;
    //    }

    unsigned int lnumphoton = numphoton;
    grid->reset_grid();
    if (log) {
      log->write_status("Start shooting photons...");
      log->write_status("Initial sub step number: ", lnumphoton, ".");
    }

    double typecount[PHOTONTYPE_NUMBER] = {0};

    unsigned int numsubstep = 0;
    unsigned int totnumphoton = 0;
    double totweight = 0.;
    while (!convergence_checker->is_converged(totnumphoton)) {
      if (log) {
        log->write_info("Substep ", numsubstep);
      }

      unsigned int local_numphoton = lnumphoton;
#ifdef HAVE_MPI
      local_numphoton = comm.distribute(local_numphoton);
#endif
      photonshootjobs.set_numphoton(local_numphoton);
      worktimer.start();
      workdistributor.do_in_parallel(photonshootjobs);
      worktimer.stop();

      totnumphoton += lnumphoton;
      photonshootjobs.update_counters(totweight, typecount);
      lnumphoton = convergence_checker->get_number_of_photons_next_substep(
          lnumphoton, totnumphoton);

      ++numsubstep;
    }
    lnumphoton = totnumphoton;
#ifdef HAVE_MPI
    totweight = comm.reduce< MPI_SUM_OF_ALL_PROCESSES >(totweight);
    comm.reduce< MPI_SUM_OF_ALL_PROCESSES, PHOTONTYPE_NUMBER >(typecount);
#endif
    if (log) {
      log->write_status("Done shooting photons.");
      log->write_status(100. * typecount[PHOTONTYPE_ABSORBED] / totweight,
                        "% of photons were reemitted as non-ionizing photons.");
      log->write_status(100. * (typecount[PHOTONTYPE_DIFFUSE_HI] +
                                typecount[PHOTONTYPE_DIFFUSE_HeI]) /
                            totweight,
                        "% of photons were scattered.");
      double escape_fraction =
          (100. * (totweight - typecount[PHOTONTYPE_ABSORBED])) / totweight;
      // since totweight is updated in chunks, while the counters are updated
      // per photon, round off might cause totweight to be slightly smaller than
      // the counter value. This gives (strange looking) negative escape
      // fractions, which we reset to 0 here.
      escape_fraction = std::max(0., escape_fraction);
      log->write_status("Escape fraction: ", escape_fraction, "%.");
      double escape_fraction_HI =
          (100. * typecount[PHOTONTYPE_DIFFUSE_HI]) / totweight;
      log->write_status("Diffuse HI escape fraction: ", escape_fraction_HI,
                        "%.");
      double escape_fraction_HeI =
          (100. * typecount[PHOTONTYPE_DIFFUSE_HeI]) / totweight;
      log->write_status("Diffuse HeI escape fraction: ", escape_fraction_HeI,
                        "%.");
    }

    if (log) {
      log->write_status("Calculating ionization state after shooting ",
                        lnumphoton, " photons...");
    }
#ifdef HAVE_MPI
    for (int i = 0; i < NUMBER_OF_IONNAMES; ++i) {
      IonName ion = static_cast< IonName >(i);
      comm.reduce< MPI_SUM_OF_ALL_PROCESSES >(
          grid->get_mean_intensity_handle(ion));
    }
    if (calculate_temperature && loop > 3) {
      comm.reduce< MPI_SUM_OF_ALL_PROCESSES >(grid->get_heating_H_handle());
      comm.reduce< MPI_SUM_OF_ALL_PROCESSES >(grid->get_heating_He_handle());
    }
#endif
    if (calculate_temperature && loop > 3) {
      temperature_calculator.calculate_temperature(totweight, *grid, block);
    } else {
      ionization_state_calculator.calculate_ionization_state(totweight, *grid,
                                                             block);
    }
#ifdef HAVE_MPI
    // the calculation above will have changed the ionic fractions, and might
    // have changed the temperatures
    // we have to gather these across all processes
    for (int i = 0; i < NUMBER_OF_IONNAMES; ++i) {
      IonName ion = static_cast< IonName >(i);
      comm.gather(grid->get_ionic_fraction_handle(ion));
    }
    if (calculate_temperature && loop > 3) {
      comm.gather(grid->get_temperature_handle());
    }
#endif
    if (log) {
      log->write_status("Done calculating ionization state.");
    }

    // calculate emissivities
    // we disabled this, since we now have the post-processing Python library
    // for this
    //    if (loop > 3 && abundances.get_abundance(ELEMENT_He) > 0.) {
    //      emissivity_calculator.calculate_emissivities(*grid);
    //    }

    // use the current number of photons as a guess for the new number
    numphoton = convergence_checker->get_new_number_of_photons(lnumphoton);

// print out a curve that shows the evolution of chi2
#ifdef CHISQUAREDPHOTONNUMBERCONVERGENCECHECKER_CHI2_CURVE
#pragma message "Outputting chi2 curve!"
    ((ChiSquaredPhotonNumberConvergenceChecker *)convergence_checker)
        ->output_chi2_curve(loop);
#endif

    ++loop;
  }

  if (log && loop == nloop) {
    log->write_status("Maximum number of iterations (", nloop,
                      ") reached, stopping.");
  }

  // write snapshot
  if (write_output) {
    writer->write(loop - 1, params);
  }

  programtimer.stop();
  if (log) {
    log->write_status("Total program time: ",
                      Utilities::human_readable_time(programtimer.value()),
                      ".");
    log->write_status("Total photon shooting time: ",
                      Utilities::human_readable_time(worktimer.value()), ".");
  }

  if (sourcedistribution != nullptr) {
    delete sourcedistribution;
  }
  if (continuoussource != nullptr) {
    delete continuoussource;
  }
  delete density_function;
  delete writer;
  delete grid;
  delete itconvergence_checker;
  delete convergence_checker;
  delete continuousspectrum;
  delete spectrum;

  // we cannot delete the log, since it is still used in the destructor of
  // objects that are destructed at the return of the main program
  // this is not really a problem, as the memory is freed up by the OS anyway
  // delete log;

  return 0;
}

/**
 * @mainpage
 *
 * @author Bert Vandenbroucke \n
 *         School of Physics and Astronomy \n
 *         University of St Andrews \n
 *         North Haugh \n
 *         St Andrews \n
 *         Fife \n
 *         KY16 9SS \n
 *         Scotland \n
 *         United Kingdom \n
 *         bv7@st-andrews.ac.uk
 *
 * @section purpose Purpose of the program.
 *
 * CMacIonize is the C++ version of Kenny Wood's photoionization code
 * (Wood, Mathis & Ercolano, 2004, MNRAS, 348, 1337). The name @c CMacIonize is
 * based on the name of Kenny Wood's code (@c mcionize, which stands for Monte
 * Carlo ionization code), but also reflects the fact that it is written in
 * C(++) (not Fortran, like the old code), and the fact that development started
 * during the first week of the main author's post doc in Scotland.
 *
 * The code can be used to perform 3D radiative transfer calculations on a
 * number of different grid structures, using various possible sources of
 * ionizing radiation with various possible spectra. One of the main goals
 * during development was to provide a very general and user-friendly interface,
 * so that the code can be used in a wide variety of possible scenarios, taking
 * input data from a wide variety of possible data formats, with a minimal
 * technical involvement of the end user. This automatically led to a highly
 * modular code design, whereby most functionality is encoded into classes with
 * a limited number of responsibilities that are covered by extensive unit
 * tests. Adding a new functionality should in principle be as simple as adding
 * a new class that implements an already defined general class interface.
 *
 * The code also offers an extensive framework of utility functions and classes
 * that simplify various aspects of new code development, like for example
 * high level wrappers around HDF5 read and write functions, and a very
 * intuitive unit system.
 *
 * The code is currently only parallelized for use on shared memory system using
 * OpenMP, but there are plans to also port it to larger distributed memory
 * systems using MPI. The current OpenMP implementation shows reasonable speed
 * ups on small systems, although no formal scaling tests have been performed
 * yet.
 *
 * @section structure Structure of the program.
 *
 * Due to time constraints, there is no extensive code manual (yet). However,
 * all files, functions, classes... in the source code are fully documented
 * (the Doxygen configuration enforces this), so most of it should be easy to
 * understand. The main program entry point can be found in the file
 * CMacIonize.cpp.
 */
