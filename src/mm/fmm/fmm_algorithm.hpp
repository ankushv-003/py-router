/**
 * Fast map matching.
 *
 * fmm algorithm implementation
 *
 * @author: Can Yang
 * @version: 2020.01.31
 */

#ifndef FASTMM_FASTMM_ALGORITHM_H_
#define FASTMM_FASTMM_ALGORITHM_H_

#include "network/network.hpp"
#include "network/network_graph.hpp"
#include "mm/transition_graph.hpp"
#include "mm/fmm/ubodt.hpp"

#include <string>
#include <optional>
#include <boost/property_tree/ptree.hpp>

namespace FASTMM
{
  namespace MM
  {

    /**
     * Configuration class for fmm algorithm
     */
    struct FastMapMatchConfig
    {
      /**
       * Constructor of FastMapMatch configuration
       * @param max_candidates the max number of candidates for each GPS point
       * @param candidate_search_radius the search radius, in map unit, which is the same as
       * GPS data and network data.
       * @param gps_error the gps error, in map unit
       * @param reverse_tolerance reverse tolerance, in map unit
       * @param transition_mode routing mode (SHORTEST or FASTEST)
       * @param reference_speed reference speed for FASTEST mode (required if mode is FASTEST)
       */
      FastMapMatchConfig(int max_candidates = 8,
                         double candidate_search_radius = 50,
                         double gps_error = 50,
                         double reverse_tolerance = 0.0,
                         TransitionMode transition_mode = TransitionMode::SHORTEST,
                         std::optional<double> reference_speed = std::nullopt,
                         double max_route_distance_factor = 0.0,
                         double turn_penalty_factor = 0.0);
      int max_candidates;                    /**< Number of candidates */
      double candidate_search_radius;        /**< Search radius, in map unit*/
      double gps_error;                      /**< GPS error, in map unit */
      double reverse_tolerance;              /**< Reverse tolerance, in map unit */
      TransitionMode transition_mode;        /**< Routing mode */
      std::optional<double> reference_speed; /**< Reference speed for FASTEST mode */
      // ---- Meili-style transition controls (0 = disabled -> identical to current behaviour) ----
      double max_route_distance_factor;      /**< reject transition if path_cost > factor*euclidean (Meili max_route_distance_factor). 0 = off */
      double turn_penalty_factor;            /**< tp *= exp(-factor*turn/180) on edge change; penalises U-turns/carriageway flips (Meili turn_penalty_factor). 0 = off */
    };

    /**
     * Fast map matching algorithm/model.
     */
    class FastMapMatch
    {
    public:
      /**
       * User-friendly constructor: takes both max_distance_between_candidates and max_time_between_candidates.
       * Only the relevant one is used depending on mode.
       * @param network road network (must be finalized)
       * @param mode transition mode (SHORTEST or FASTEST)
       * @param max_distance_between_candidates maximum distance in meters (for SHORTEST mode)
       * @param max_time_between_candidates maximum time in seconds (for FASTEST mode)
       * @param cache_dir directory for UBODT cache files
       * @throws std::invalid_argument if parameters are invalid
       * @throws std::runtime_error if UBODT generation/loading fails
       */
      FastMapMatch(const NETWORK::Network &network,
                   TransitionMode mode,
                   std::optional<double> max_distance_between_candidates = std::nullopt,
                   std::optional<double> max_time_between_candidates = std::nullopt,
                   const std::string &cache_dir = "./ubodt_cache");

      /**
       * Constructor that takes a pre-loaded UBODT instead of managing one via cache.
       * Useful for sharing a single UBODT across multiple matchers or for fully
       * controlling UBODT lifecycle from Python.
       * @param network road network (must be finalized)
       * @param mode transition mode (SHORTEST or FASTEST) - must match the UBODT's mode
       * @param ubodt pre-loaded UBODT (network hash and mode are validated against the network)
       * @throws std::invalid_argument if network is not finalized or has no edges
       * @throws std::runtime_error if the UBODT does not match the network
       */
      FastMapMatch(const NETWORK::Network &network,
                   TransitionMode mode,
                   std::shared_ptr<UBODT> ubodt);

      /**
       * Match a trajectory with automatic splitting on failures
       * Performs candidate search once and reuses it for all sub-trajectories
       * @param  trajectory   input trajectory data
       * @param  max_candidates the number of candidates
       * @param  candidate_search_radius the search radius, in map unit
       * @param  gps_error the gps error, in map unit
       * @param  reverse_tolerance reverse tolerance, in map unit
       * @param  reference_speed reference speed for FASTEST mode (optional)
       * @return split match result containing all successful and failed sub-trajectories
       */
      PySplitMatchResult pymatch_trajectory(const CORE::Trajectory &trajectory,
                                            int max_candidates,
                                            double candidate_search_radius,
                                            double gps_error,
                                            double reverse_tolerance,
                                            std::optional<double> reference_speed,
                                            double max_route_distance_factor = 0.0,
                                            double turn_penalty_factor = 0.0);

    protected:
      /**
       * Match a trajectory to the road network
       * @param  trajectory   input trajector data
       * @param  config configuration of map matching algorithm
       * @return map matching result
       */
      MatchResult match_trajectory(const CORE::Trajectory &trajectory,
                                   const FastMapMatchConfig &config);

      /**
       * Get path distance between two candidates (always distance, regardless of mode)
       * @param  ca from candidate
       * @param  cb to candidate
       * @param  reverse_tolerance reverse tolerance parameter (in map units)
       * @return  path distance
       */
      double get_distance(const Candidate *ca,
                          const Candidate *cb,
                          double reverse_tolerance);

      /**
       * Get path time between two candidates (distance/speed for each segment)
       * @param  ca from candidate
       * @param  cb to candidate
       * @param  reverse_tolerance reverse tolerance parameter (in map units)
       * @return  path time
       */
      double get_time(const Candidate *ca,
                      const Candidate *cb,
                      double reverse_tolerance);

      /**
       * Update probabilities in a transition graph
       * @param tg transition graph
       * @param trajectory raw trajectory
       * @param config map match configuration
       * @param all_connected output parameter indicating if all layers are connected
       * @return idx of the last connected layer
       */
      int update_tg(TransitionGraph *tg, const CORE::Trajectory &trajectory, const FastMapMatchConfig &config, bool *all_connected);
      /**
       * Update probabilities between two layers a and b in the transition graph
       * @param level   the index of layer a
       * @param la_ptr  layer a
       * @param lb_ptr  layer b next to a
       * @param euclidean_distance Euclidean distance between two observed point
       * @param config map match configuration
       * @param connected the variable is set to false if the layer is not connected
       * with the next layer
       */
      void update_layer(int level, TGLayer *la_ptr, TGLayer *lb_ptr,
                        double euclidean_distance,
                        const FastMapMatchConfig &config,
                        bool *connected);
      /**
       * Build PyMatchSegment objects from matched path
       * Helper function for building segment output, extracted to avoid duplication
       * @param matched_path matched candidate path
       * @param complete_path complete edge path
       * @param indices indices mapping
       * @param trajectory original trajectory
       * @param start_idx starting trajectory index
       * @param end_idx ending trajectory index
       * @return vector of PyMatchSegment objects
       */
      std::vector<PyMatchSegment> build_py_segments(const MatchedCandidatePath &matched_path,
                                                    const CompletePath &complete_path,
                                                    const std::vector<int> &indices,
                                                    const CORE::Trajectory &trajectory,
                                                    int start_idx,
                                                    int end_idx);

    private:
      const NETWORK::Network &network_;
      NETWORK::NetworkGraph graph_;
      std::shared_ptr<UBODT> ubodt_;
      TransitionMode mode_;
    };
  }
}

#endif
