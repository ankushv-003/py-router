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

#include <string>
#include <vector>

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
      int trajectory_index;       /**< 0-based index of the trajectory observation point this candidate was matched from */
    };

    struct PyMatchSegment
    {
      PyMatchCandidate p0;
      PyMatchCandidate p1;
      std::vector<PyMatchSegmentEdge> edges;
    };

    /**
     * Why consecutive points i -> i+1 are not in one matched piece (a break). The classes point at
     * the likely cause so breaks aggregated over many trips can flag map problems.
     */
    enum class BreakType : int
    {
      NO_ROAD = 1,        /**< a point of the interval has no candidate within the search radius (road missing, off-road, premises) */
      BACKWARD = 2,       /**< nearest candidates on the same edge, the second at most max(gps_error, reverse_tolerance) behind the first (GPS jitter) */
      ONEWAY_CONTRA = 3,  /**< nearest road runs against the movement, has no reverse edge, and no separate parallel road runs with the movement nearby: wrong-way riding or a wrong one-way tag */
      DIVIDED_CONTRA = 4, /**< nearest road runs against the movement, and a parallel road with the movement that is not attached to it is near either point: other carriageway of a divided road (median crossing, missing opening) */
      DETOUR = 5,         /**< aligned roads on both sides, but the legal route is longer than the route gate allows: missing turn / link / median opening */
      UNREACHABLE = 6,    /**< aligned roads on both sides, but no legal route within the UBODT bound: missing connection or one-way trap */
      OTHER = 9           /**< anything else (should not happen) */
    };

    /**
     * One break interval (point index -> index + 1)
     */
    struct PyBreak
    {
      int index = -1;                       /**< 0-based index of the first point of the interval */
      BreakType type = BreakType::OTHER;    /**< likely cause */
      double route_ratio = -1.0;            /**< shortest legal route / straight line over candidate pairs (DETOUR); -1 if not computed or unreachable */
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
      std::vector<PyBreak> breaks;                  /**< Every interval not inside one successful piece, in order, with its likely cause */
    };

    /**
     * Fully materialized per-trip match output — the ETL's CSV/flag columns computed
     * natively (no Python object tree, no GIL). cpath/opath/length/duration are already
     * comma-joined so the Python consumer writes them verbatim. snap_flag is intentionally
     * NOT computed here: it depends on the caller's candidate radius, so the caller derives
     * it from match_status/n_reversed/max_snap_dist_deg (one boolean, no iteration).
     */
    struct PyMatchRows
    {
      std::string cpath;        /**< comma-joined deduped edge IDs */
      std::string opath;        /**< comma-joined edge ID per ORIGINAL ping (-1 where dropped); length == orig_len */
      std::string length;       /**< comma-joined per-folded-edge length, "%.6f" */
      std::string duration;     /**< comma-joined per-folded-edge duration (s), "%.3f" */
      std::string match_status; /**< "full" | "partial" | "failed" */
      int n_sub = 0;            /**< total sub-trajectories (== Python n_sub) */
      int n_reversed = 0;       /**< reversed edges over successful segments */
      double max_snap_dist_deg = 0.0; /**< max perpendicular snap distance over successful segments */
      // Piece/break information (additive; cpath and match_status are unchanged).
      int n_pieces = 0;          /**< successfully matched sub-trajectories (pieces) */
      std::string piece_starts;  /**< comma-joined cpath index where each piece starts (shares the entry if a piece starts on the edge the previous one ended on) */
      int n_pts = 0;             /**< points in the (cleaned) input trajectory */
      int n_pts_matched = 0;     /**< points inside a successful piece */
      double gap_length = 0.0;   /**< straight-line length (coordinate units) of every point-to-point interval NOT inside one piece: the break metres cpath leaves out */
      std::string break_idx;     /**< comma-joined index i of every break interval (i -> i+1) */
      std::string break_type;    /**< comma-joined BreakType code of every break interval (same order) */
    };
  };

};

#endif
