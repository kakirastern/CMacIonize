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
 * @file testFLASHSnapshotDensityFunction.cpp
 *
 * @brief Unit test for the FLASHSnapshotDensityFunction class.
 *
 * @author Bert Vandenbroucke (bv7@st-andrews.ac.uk)
 */
#include "FLASHSnapshotDensityFunction.hpp"
#include <fstream>

/**
 * @brief Unit test for the FLASHSnapshotDensityFunction class.
 *
 * @param argc Number of command line arguments.
 * @param argv Command line arguments.
 * @return Exit code: 0 on success.
 */
int main(int argc, char **argv) {
  FLASHSnapshotDensityFunction density("SILCC_hdf5_plt_cnt_0000");

  std::ofstream ofile("slice_z.txt");
  unsigned int np = 1024;
  for (unsigned int i = 0; i < np; ++i) {
    CoordinateVector<> p(0., 0., -3.8575e20 + (i + 0.5) * 7.715e20 / np);
    ofile << p.z() << "\t" << density(p) << "\n";
  }

  return 0;
}
