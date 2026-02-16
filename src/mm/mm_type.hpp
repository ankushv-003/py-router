/**
 * Fast map matching.
 *
 * Definition of map matching types
 *
 * @author: Can Yang
 * @version: 2020.01.31
 */

#ifndef FASTMM_INCLUDE_FASTMM_FASTMM_RESULT_HPP_
#define FASTMM_INCLUDE_FASTMM_FASTMM_RESULT_HPP_

#include "network/type.hpp"

namespace FASTMM
{

  /**
   * Class related with map matching
   */
  namespace MM
  {

    /**
     * Routing mode for map matching
     */
    enum class TransitionMode
    {
      SHORTEST, /**< Distance-based routing */
      FASTEST   /**< Time-based routing */
    };

    /**
     * Error codes for map matching results
     */
    enum class MatchErrorCode : int
    {
      SUCCESS = 0,                 /**< Matching succeeded */
      CANDIDATES_NOT_FOUND = 1,    /**< No candidate edges found for trajectory */
      DISCONNECTED_LAYERS = 2,     /**< Trajectory has disconnected layers (can't find valid path between some GPS points) */
      INDEX_OUT_OF_BOUNDS = 3,     /**< Internal error: start edge index out of bounds */
      INDEX_OUT_OF_BOUNDS_END = 4, /**< Internal error: end edge index out of bounds */
      UNKNOWN_ERROR = 255          /**< Unknown error occurred */
    };

    /**
     * %Candidate edge matched to a GPS point
     */
    struct Candidate
    {
      FASTMM::NETWORK::NodeIndex index; /**< The index is defined for a specific candidate the index starting from N where N is the number of vertices in the graph */
      double offset;                    /**< offset distance from the start of polyline to p' */
      double dist;                      /**< distance from original point p to map matched point p' */
      NETWORK::Edge *edge;              /**< candidate edge */
      FASTMM::CORE::Point point;        /**< boost point */
    };

    typedef std::vector<Candidate> PointCandidates; /**< Point candidates */
    typedef std::vector<PointCandidates> TrajectoryCandidates;
    /**< trajectory  candidates */

    typedef std::vector<const Candidate *> OptCandidatePath;
    /**< Optimal candidates*/

    typedef std::vector<FASTMM::NETWORK::EdgeID> OptimalPath;  /**< Optimal path, edge id matched to each point in the trajectory */
    typedef std::vector<FASTMM::NETWORK::EdgeID> CompletePath; /**< Complete path, ids of a sequence of topologically connected edges.*/

    /**
     * A candidate matched to a point
     */
    struct MatchedCandidate
    {
      Candidate c;                   /**< Candidate matched to a point */
      double ep;                     /**< emission probability */
      double tp;                     /**< transition probability to previous matched candidate */
      double shortest_path_distance; /**< shortest path distance to previous matched candidate */
    };

    /**
     * A vector of candidates, used for representing the matching result of
     * a trajectory.
     */
    typedef std::vector<MatchedCandidate> MatchedCandidatePath;

    /**
     * Map matched result representation
     */
    struct MatchResult
    {
      MatchedCandidatePath opt_candidate_path; /**< A vector of candidate matched to each point of a trajectory. It is stored in order to export more detailed map matching information. */
      OptimalPath optimal_path;                /**< the optimal path, containing id of edges matched to each point in a trajectory */
      CompletePath complete_path;              /**< the complete path, containing ids of a sequence of topologically connected edges traversed by the trajectory.  */
      std::vector<int> indices;                /**< index of optimal_path edge in complete_path */
      CORE::LineString matched_geometry;       /**< the geometry of the matched path */
      MatchErrorCode error_code;               /**< error code if the map matching failed */
    };

    struct PyMatchPoint
    {
      double x;                   //**< x coordinate */
      double y;                   //**< y coordinate */
      double d;                   //**< Distance from previous point along the edge */
      double t;                   //**< Infer the time at this point if possible. Speeds are used if available. -1 if unknown */
      double speed;               //**< Speed at this point (from edge), if available, -1 if unknown */
      double edge_offset;         //**< Offset from start of edge */
      double cumulative_distance; //**< Cumulative distance along the matched path */
    };

    struct PyMatchSegmentEdge
    {
      NETWORK::EdgeID edge_id;
      std::vector<PyMatchPoint> points;
      bool reversed; // True if geometry is reversed (offset1 > offset2 on same edge)
    };

    struct PyMatchCandidate
    {
      double x;
      double y;
      double t; //**< Timestamp or time value, -1 if unknown */
      double perpendicular_distance_to_matched_geometry;
      double offset_from_start_of_edge;
      int trajectory_index;       //**< 1-based trajectory point index for trajectory observation points, 0 for interpolated points */
    };

    struct PyMatchSegment
    {
      PyMatchCandidate p0;
      PyMatchCandidate p1;
      std::vector<PyMatchSegmentEdge> edges;
    };

    /**
     * A continuous sub-trajectory match result (portion of trajectory that could be matched)
     */
    struct PySubTrajectory
    {
      int start_index;                      /**< Starting trajectory point index (inclusive) */
      int end_index;                        /**< Ending trajectory point index (inclusive) */
      MatchErrorCode error_code;            /**< SUCCESS if matched, or reason for failure */
      std::vector<PyMatchSegment> segments; /**< Matched segments (only populated if error_code == SUCCESS) */
    };

    /**
     * Result of matching with automatic trajectory splitting
     */
    struct PySplitMatchResult
    {
      std::vector<PySubTrajectory> subtrajectories; /**< List of sub-trajectory matches (both successful and failed) */
    };
  };

};

#endif
