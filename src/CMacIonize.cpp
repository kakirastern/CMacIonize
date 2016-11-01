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
#include "Box.hpp"
#include "CommandLineOption.hpp"
#include "CommandLineParser.hpp"
#include "CoordinateVector.hpp"
#include "DensityFunctionFactory.hpp"
#include "DensityGrid.hpp"
#include "DensityGridWriterFactory.hpp"
#include "FileLog.hpp"
#include "LineCoolingData.hpp"
#include "ParameterFile.hpp"
#include "PhotonSource.hpp"
#include "PhotonSourceDistributionFactory.hpp"
#include "PlanckPhotonSourceSpectrum.hpp"
#include "TerminalLog.hpp"
#include "Timer.hpp"
#include "VernerCrossSections.hpp"
#include "VernerRecombinationRates.hpp"
#include <iostream>
#include <string>
using namespace std;

/**
 * @brief Entrance point of the program
 *
 * @param argc Number of command line arguments passed on to the program.
 * @param argv Array containing the command line arguments.
 * @return Exit code: 0 on success.
 */
int main(int argc, char **argv) {
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
  parser.parse_arguments(argc, argv);

  LogLevel loglevel = LOGLEVEL_STATUS;
  if (parser.get_value< bool >("verbose")) {
    loglevel = LOGLEVEL_INFO;
  }
  Log *log;
  if (parser.was_found("logfile")) {
    log = new FileLog(parser.get_value< std::string >("logfile"), loglevel);
  } else {
    log = new TerminalLog(loglevel);
  }

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
  DensityGrid grid(params, *density_function, recombination_rates, log);

  // fifth: construct the stellar sources. These should be stored in a
  // separate StellarSources object with geometrical and physical properties.
  PhotonSourceDistribution *sourcedistribution =
      PhotonSourceDistributionFactory::generate(params, log);
  RandomGenerator random_generator;
  PlanckPhotonSourceSpectrum spectrum(random_generator);
  PhotonSource source(*sourcedistribution, spectrum, cross_sections,
                      random_generator, log);

  // set up output
  DensityGridWriter *writer =
      DensityGridWriterFactory::generate(params, grid, log);

  unsigned int nloop =
      params.get_value< unsigned int >("iterations.maxnumber", 10);
  double chirel = params.get_value< double >("iterations.tolerance", 0.01);

  unsigned int numphoton =
      params.get_value< unsigned int >("number of photons", 1000000);
  double Q = sourcedistribution->get_total_luminosity();

  // we are done reading the parameter file
  // now output all parameters (also those for which default values were used)
  // to a reference parameter file
  ofstream pfile("parameters-usedvalues.param");
  params.print_contents(pfile);
  pfile.close();

  // finally: the actual program loop whereby the density grid is ray traced
  // using photon packets generated by the stellar sources

  double old_chi2 = 0.;
  double chidiff = 1.;
  unsigned int loop = 0;
  while (loop < nloop && (chidiff > 0. || chidiff < -chirel)) {
    unsigned int lnumphoton = numphoton;
    if (loop > 4) {
      lnumphoton *= 10;
    }

    // timing information for user
    unsigned int nguess = 0.01 * lnumphoton;
    unsigned int ninfo = 0.1 * lnumphoton;
    Timer guesstimer;

    grid.reset_grid();
    source.set_number_of_photons(lnumphoton);
    log->write_status("Start shooting photons...");
    unsigned int typecount[PHOTONTYPE_NUMBER] = {0};
    for (unsigned int i = 0; i < lnumphoton; ++i) {
      if (!(i % ninfo)) {
        log->write_status("Photon ", i, " of ", lnumphoton, ".");
      }
      if (i == nguess) {
        unsigned int tguess = round(99. * guesstimer.stop());
        log->write_status("Shooting photons will take approximately ", tguess,
                          " seconds.");
      }
      Photon photon = source.get_random_photon();
      double tau = -std::log(Utilities::random_double());
      while (grid.interact(photon, tau)) {
        CoordinateVector< int > new_index =
            grid.get_cell_indices(photon.get_position());
        if (!source.reemit(photon, grid.get_cell_values(new_index),
                           params.get_value< double >("helium_abundance"))) {
          break;
        }
        tau = -std::log(Utilities::random_double());
      }
      ++typecount[photon.get_type()];
    }
    log->write_status("Done shooting photons.");
    log->write_status(typecount[PHOTONTYPE_ABSORBED],
                      " photons were reemitted as non-ionizing photons.");
    log->write_status(typecount[PHOTONTYPE_DIFFUSE_HI] +
                          typecount[PHOTONTYPE_DIFFUSE_HeI],
                      " photons were scattered.");
    double escape_fraction =
        (100. * (lnumphoton - typecount[PHOTONTYPE_ABSORBED])) / lnumphoton;
    log->write_status("Escape fraction: ", escape_fraction, "%.");
    double escape_fraction_HI =
        (100. * typecount[PHOTONTYPE_DIFFUSE_HI]) / lnumphoton;
    log->write_status("Diffuse HI escape fraction: ", escape_fraction_HI, "%.");
    double escape_fraction_HeI =
        (100. * typecount[PHOTONTYPE_DIFFUSE_HeI]) / lnumphoton;
    log->write_status("Diffuse HeI escape fraction: ", escape_fraction_HeI,
                      "%.");
    grid.calculate_ionization_state(Q, lnumphoton);

    double chi2 = grid.get_chi_squared();
    log->write_status("Chi2: ", chi2, ".");
    chidiff = (chi2 - old_chi2) / (chi2 + old_chi2);
    log->write_status("Chidiff: ", chidiff, ".");
    old_chi2 = chi2;
    // write snapshot
    writer->write(loop);
    ++loop;
  }

  if (loop == nloop) {
    log->write_status("Maximum number of iterations (", nloop,
                      ") reached, stopping.");
  }
  if ((chidiff < 0. && chidiff > -chirel)) {
    log->write_status("Required tolerance (", (-chidiff),
                      ") reached, stopping.");
  }

  programtimer.stop();
  log->write_status("Total program time: ", programtimer.value(), " s.");

  delete sourcedistribution;
  delete density_function;
  delete writer;

  // we cannot delete the log, since it is still used in the destructor of
  // objects that are destructed at the return of the main program
  // this is not really a problem, as the memory is freed up by the OS anyway
  // delete log;

  return 0;
}
