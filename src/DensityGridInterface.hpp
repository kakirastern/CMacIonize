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
 * @file DensityGridInterface.hpp
 *
 * @brief General interface for density grids.
 *
 * @author Bert Vandenbroucke (bv7@st-andrews.ac.uk)
 */
#ifndef DENSITYGRIDINTERFACE_HPP
#define DENSITYGRIDINTERFACE_HPP

#include "Box.hpp"
#include "CoordinateVector.hpp"
#include "DensityValues.hpp"

class Log;

/**
 * @brief General interface for density grids.
 */
class DensityGridInterface {
protected:
  /*! @brief Box containing the grid. */
  Box _box;

  /*! @brief Periodicity flags. */
  CoordinateVector< bool > _periodic;

  /*! @brief Log to write log messages to. */
  Log *_log;

  /**
   * @brief Set the re-emission probabilities for the given cell for the given
   * temperature.
   *
   * These quantities are all dimensionless.
   *
   * @param T Temperature (in K).
   * @param cell DensityValues of the cell.
   */
  void set_reemission_probabilities(double T, DensityValues &cell) {
    double alpha_1_H = 1.58e-13 * pow(T * 1.e-4, -0.53);
    double alpha_A_agn = 4.18e-13 * pow(T * 1.e-4, -0.7);
    cell.set_pHion(alpha_1_H / alpha_A_agn);

    double alpha_1_He = 1.54e-13 * pow(T * 1.e-4, -0.486);
    double alpha_e_2tS = 2.1e-13 * pow(T * 1.e-4, -0.381);
    double alpha_e_2sS = 2.06e-14 * pow(T * 1.e-4, -0.451);
    double alpha_e_2sP = 4.17e-14 * pow(T * 1.e-4, -0.695);
    double alphaHe = 4.27e-13 * pow(T * 1.e-4, -0.678);
    // we overwrite the alphaHe value. This also guarantees that the sum of all
    // probabilities is 1...
    alphaHe = alpha_1_He + alpha_e_2tS + alpha_e_2sS + alpha_e_2sP;

    cell.set_pHe_em(0, alpha_1_He / alphaHe);
    cell.set_pHe_em(1, cell.get_pHe_em(0) + alpha_e_2tS / alphaHe);
    cell.set_pHe_em(2, cell.get_pHe_em(1) + alpha_e_2sS / alphaHe);
    cell.set_pHe_em(3, cell.get_pHe_em(2) + alpha_e_2sP / alphaHe);
  }

public:
  /**
   * @brief Constructor.
   *
   * @param box Box containing the grid.
   * @param periodic Periodicity flags.
   * @param log Log to write log messages to.
   */
  DensityGridInterface(Box box, CoordinateVector< bool > periodic =
                                    CoordinateVector< bool >(false),
                       Log *log = nullptr)
      : _box(box), _periodic(periodic), _log(log) {}

  /**
   * @brief Initialize the given cell.
   *
   * @param initial_temperature Initial temperature (in K).
   * @param helium_abundance Helium abundance.
   * @param cell Cell to initialize.
   */
  void initialize(double initial_temperature, double helium_abundance,
                  DensityValues &cell) {
    cell.set_neutral_fraction_H(1.e-6);
    cell.set_neutral_fraction_He(1.e-6);
    cell.set_temperature(initial_temperature);
    cell.set_helium_abundance(helium_abundance);
    set_reemission_probabilities(initial_temperature, cell);
  }

  virtual ~DensityGridInterface() {}

  /**
   * @brief Get the midpoint of the cell with the given index.
   *
   * @param index Index of a cell.
   * @return Midpoint of that cell (in m).
   */
  virtual CoordinateVector<> get_cell_midpoint(unsigned long index) = 0;

  /**
   * @brief Get the values stored in the cell with the given index.
   *
   * @param index Index of a cell.
   * @return DensityValues stored in that cell.
   */
  virtual DensityValues &get_cell_values(unsigned long index) = 0;

  /**
   * @brief Get the volume of the cell with the given index.
   *
   * @param index Index of a cell.
   * @return Volume of that cell (in m^3).
   */
  virtual double get_cell_volume(unsigned long index) = 0;

  /**
   * @brief Iterator to loop over the cells in the grid.
   */
  class iterator {
  private:
    /*! @brief Long index of the cell the iterator is currently pointing to. */
    unsigned long _long_index;

    /*! @brief Reference to the DensityGrid over which we iterate. */
    DensityGridInterface &_grid;

  public:
    /**
     * @brief Constructor.
     *
     * @param long_index Long index of the cell the iterator is currently
     * pointing to.
     * @param grid DensityGrid over which we iterate.
     */
    inline iterator(unsigned long long_index, DensityGridInterface &grid)
        : _long_index(long_index), _grid(grid) {}

    /**
     * @brief Get the midpoint of the cell the iterator is pointing to.
     *
     * @return Cell midpoint (in m).
     */
    inline CoordinateVector<> get_cell_midpoint() {
      return _grid.get_cell_midpoint(_long_index);
    }

    /**
     * @brief Get the DensityValues of the cell the iterator is pointing to.
     *
     * @return DensityValue the iterator is pointing to.
     */
    inline DensityValues &get_values() {
      return _grid.get_cell_values(_long_index);
    }

    /**
     * @brief Get the volume of the cell the iterator is pointing to.
     *
     * @return Volume of the cell (in m^3).
     */
    inline double get_volume() { return _grid.get_cell_volume(_long_index); }

    /**
     * @brief Increment operator.
     *
     * We only implemented the pre-increment version, since the post-increment
     * version creates a new object and is computationally more expensive.
     *
     * @return Reference to the incremented iterator.
     */
    inline iterator &operator++() {
      ++_long_index;
      return *this;
    }

    /**
     * @brief Compare iterators.
     *
     * @param it Iterator to compare with.
     * @return True if the iterators point to the same cell of the same grid.
     */
    inline bool operator==(iterator it) {
      return (&_grid == &it._grid && _long_index == it._long_index);
    }

    /**
     * @brief Compare iterators.
     *
     * @param it Iterator to compare with.
     * @return True if the iterators do not point to the same cell of the same
     * grid.
     */
    inline bool operator!=(iterator it) { return !(*this == it); }
  };

  /**
   * @brief Get an iterator to the first cell in the grid.
   *
   * @return Iterator to the first cell in the grid.
   */
  virtual iterator begin() = 0;

  /**
   * @brief Get an iterator to the last cell in the grid.
   *
   * @return Iterator to the last cell in the grid.
   */
  virtual iterator end() = 0;
};

#endif // DENSITYGRIDINTERFACE_HPP
