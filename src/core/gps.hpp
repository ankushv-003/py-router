/**
 * Fast map matching.
 *
 * Definition of input trajectory format
 *
 * @author: Can Yang
 * @version: 2017.11.11
 */

#ifndef FASTMM_GPS_HPP
#define FASTMM_GPS_HPP

#include "core/geometry.hpp"

#include <vector>

namespace FASTMM
{

  namespace CORE
  {

    /**
     * %Trajectory class
     *
     * A GPS trajectory represented with id, geometry and timestamps
     */
    struct Trajectory
    {
      Trajectory() : has_timestamps_(false) {};
      Trajectory(const LineString &geom_arg) : geom(geom_arg), has_timestamps_(false)
      {
        if (geom.get_num_points() == 0)
        {
          throw std::invalid_argument("Trajectory: must have at least one point!");
        }
      };
      Trajectory(const LineString &geom_arg, const std::vector<double> &timestamps_arg)
          : geom(geom_arg), timestamps(timestamps_arg), has_timestamps_(true)
      {
        if (geom.get_num_points() != static_cast<int>(timestamps.size()))
        {
          throw std::invalid_argument("Trajectory: timestamps and geometry must have the same length");
        }
        if (geom.get_num_points() == 0)
        {
          throw std::invalid_argument("Trajectory: must have at least one point/timestamp!");
        }

        // Ensure timestamps are sorted and > 0:
        double prev_t = timestamps[0];
        for (size_t i = 1; i < timestamps.size(); ++i)
        {
          double t = timestamps[i];
          if (t < 0)
          {
            throw std::invalid_argument("Trajectory: timestamps must be non-negative");
          }
          if (t < prev_t)
          {
            throw std::invalid_argument("Trajectory: timestamps must be non-decreasing");
          }
          prev_t = t;
        }
      }
      LineString geom;                /**< Geometry of the trajectory */
      std::vector<double> timestamps; /**< Timestamps of the trajectory */

      bool has_timestamps() const
      {
        return has_timestamps_;
      }

      // Return the number of points in the trajectory
      int size() const
      {
        return geom.get_num_points();
      }

      // Create a Trajectory from a vector of (x, y) tuples
      static Trajectory from_xy_tuples(const std::vector<std::tuple<double, double>> &data)
      {
        LineString geom;
        for (const auto &item : data)
        {
          geom.add_point(Point(std::get<0>(item), std::get<1>(item)));
        }
        return Trajectory(geom);
      }

      // Create a Trajectory from a vector of (x, y, t) tuples
      static Trajectory from_xyt_tuples(const std::vector<std::tuple<double, double, double>> &data)
      {
        LineString geom;
        std::vector<double> timestamps;
        for (const auto &item : data)
        {
          geom.add_point(Point(std::get<0>(item), std::get<1>(item)));
          timestamps.push_back(std::get<2>(item));
        }
        return Trajectory(geom, timestamps);
      }

      // Return as a vector of (x, y, t) tuples
      std::vector<std::tuple<double, double, double>> to_xyt_tuples() const
      {
        if (!has_timestamps_)
        {
          throw std::runtime_error("Trajectory::to_xyt_tuples: trajectory has no timestamps");
        }
        std::vector<std::tuple<double, double, double>> data;
        int n = geom.get_num_points();
        for (int i = 0; i < n; ++i)
        {
          double x = geom.get_x(i);
          double y = geom.get_y(i);
          double t = timestamps[i];
          data.emplace_back(x, y, t);
        }
        return data;
      }

      // Return as a vector of (x, y, t) tuples
      std::vector<std::tuple<double, double>> to_xy_tuples() const
      {
        std::vector<std::tuple<double, double>> data;
        int n = geom.get_num_points();
        for (int i = 0; i < n; ++i)
        {
          double x = geom.get_x(i);
          double y = geom.get_y(i);
          data.emplace_back(x, y);
        }
        return data;
      }

    protected:
      bool has_timestamps_;
    };

  }

}
#endif
