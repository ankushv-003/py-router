// fastmm_bindings.cpp: pybind11 bindings for FASTMM
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "core/geometry.hpp"
#include "core/gps.hpp"
#include "network/type.hpp"
#include "network/network.hpp"
#include "network/network_graph.hpp"
#include "mm/mm_type.hpp"
#include "mm/fmm/fmm_algorithm.hpp"
#include "mm/fmm/ubodt_gen_algorithm.hpp"
#include "mm/fmm/ubodt.hpp"

namespace py = pybind11;
using namespace FASTMM;
using namespace FASTMM::CORE;
using namespace FASTMM::NETWORK;
using namespace FASTMM::MM;

PYBIND11_MODULE(fastmm, m)
{
    m.doc() = "Fast Map Matching (FASTMM) Python bindings via pybind11";

    // MatchErrorCode enum
    py::enum_<MatchErrorCode>(m, "MatchErrorCode")
        .value("SUCCESS", MatchErrorCode::SUCCESS, "Matching succeeded")
        .value("CANDIDATES_NOT_FOUND", MatchErrorCode::CANDIDATES_NOT_FOUND, "No candidate edges found for trajectory")
        .value("DISCONNECTED_LAYERS", MatchErrorCode::DISCONNECTED_LAYERS, "Trajectory has disconnected layers")
        .value("INDEX_OUT_OF_BOUNDS", MatchErrorCode::INDEX_OUT_OF_BOUNDS, "Start edge index out of bounds")
        .value("INDEX_OUT_OF_BOUNDS_END", MatchErrorCode::INDEX_OUT_OF_BOUNDS_END, "End edge index out of bounds")
        .value("UNKNOWN_ERROR", MatchErrorCode::UNKNOWN_ERROR, "Unknown error occurred")
        .export_values();

    // TransitionMode enum
    py::enum_<TransitionMode>(m, "TransitionMode")
        .value("SHORTEST", TransitionMode::SHORTEST, "Distance-based routing")
        .value("FASTEST", TransitionMode::FASTEST, "Time-based routing")
        .export_values();

    // Network class
    py::class_<Network>(m, "Network", R"pbdoc(
        A road network consisting of nodes (junctions) and directed edges (road segments).

        The network must be fully constructed (all edges added) before building the spatial
        index. Once the index is built, the network is ready for map matching operations.

        Example:
            >>> network = fastmm.Network()
            >>> network.add_edge(1, source=10, target=20, geom=[(0, 0), (100, 0)], speed=50.0)
            >>> network.finalize()
    )pbdoc")
        .def(py::init<>(), R"pbdoc(
            Create an empty network.

            Use add_edge() to populate the network with road segments, then call
            finalize() to prepare it for map matching.
        )pbdoc")
        .def("add_edge", [](Network &self, EdgeID edge_id, NodeID source, NodeID target, py::list coords, std::optional<double> speed)
             {
            LineString geom;
            for (auto item : coords) {
                if (py::isinstance<py::tuple>(item) && py::len(item) == 2) {
                    py::tuple tup = item.cast<py::tuple>();
                    double x = py::float_(tup[0]);
                    double y = py::float_(tup[1]);
                    geom.add_point(x, y);
                } else {
                    throw std::runtime_error("Each coordinate must be a tuple (x, y)");
                }
            }
            if (geom.get_num_points() < 2) {
                throw std::runtime_error("Edge geometry must have at least 2 points");
            }
            self.add_edge(edge_id, source, target, geom, speed); }, py::arg("edge_id"), py::arg("source"), py::arg("target"), py::arg("geom"), py::arg("speed") = std::nullopt,
             R"pbdoc(
            Add a directed edge (road segment) to the network.

            Each edge must have a unique ID and connects two nodes. The geometry defines
            the spatial path of the edge. Speed is required for FASTEST routing mode.

            Args:
                edge_id: Unique identifier for this edge (long long)
                source: Node ID where the edge starts (long long)
                target: Node ID where the edge ends (long long)
                geom: List of (x, y) tuples defining the edge geometry (minimum 2 points)
                speed: Optional speed value (distance units per time unit).
                       Required if using TransitionMode.FASTEST routing.

            Note:
                Call finalize() after adding all edges.

            Example:
                >>> network.add_edge(1, source=1, target=2, geom=[(0, 0), (100, 0)], speed=50.0)
        )pbdoc")
        .def("finalize", &Network::finalize,
             R"pbdoc(
            Build the spatial R-tree index for efficient candidate edge lookup.

            This MUST be called after adding all edges and before creating a NetworkGraph
            or performing any map matching operations. The index enables fast spatial
            queries to find nearby road segments for GPS points.

            Raises:
                RuntimeError: If called on an empty network
        )pbdoc")
        .def("compute_hash", &Network::compute_hash,
             R"pbdoc(
            Compute a hash of the network structure for cache validation.

            The hash is computed from edge count, sampled edge IDs, sources,
            targets, and speeds. It's used to detect network changes and
            invalidate cached UBODT files.

            Returns:
                str: 8-character hexadecimal hash string
        )pbdoc")
        .def("get_edge_count", &Network::get_edge_count, R"pbdoc(
            Get the total number of edges in the network.

            Returns:
                int: Number of edges
        )pbdoc")
        .def("get_node_count", &Network::get_node_count, R"pbdoc(
            Get the total number of nodes in the network.

            Returns:
                int: Number of unique nodes
        )pbdoc");

    // NetworkGraph class
    py::class_<NetworkGraph>(m, "NetworkGraph", R"pbdoc(
        A routing graph built from a Network for path finding.

        The graph representation depends on the chosen routing mode:
        - SHORTEST: Uses edge distances for path costs
        - FASTEST: Uses edge travel times (distance/speed) for path costs

        A separate graph must be created for each routing mode, and FASTEST mode
        requires that all edges have speed values defined.
    )pbdoc")
        .def(py::init<const Network &, TransitionMode>(),
             py::arg("network"),
             py::arg("mode") = TransitionMode::SHORTEST,
             R"pbdoc(
            Create a NetworkGraph from a Network with specified routing mode.

            Args:
                network: Network with edges (must have finalize() called)
                mode: Routing mode - SHORTEST (distance-based) or FASTEST (time-based).
                      Default is SHORTEST.

            Raises:
                RuntimeError: If mode is FASTEST and any edge lacks a speed value

            Note:
                Generate separate UBODT files for each routing mode, as the shortest
                paths differ between distance-based and time-based routing.
        )pbdoc");

    // MatchPoint struct
    py::class_<PyMatchPoint>(m, "MatchPoint", R"pbdoc(
        A matched point along a road edge in the map matching result.

        Represents a specific location on a matched edge, including its position
        relative to the edge start and the cumulative distance from the trajectory start.
    )pbdoc")
        .def_readonly("x", &PyMatchPoint::x, "X coordinate of the matched point")
        .def_readonly("y", &PyMatchPoint::y, "Y coordinate of the matched point")
        .def_readonly("d", &PyMatchPoint::d,
                      "Distance from the previous matched point along the edge (coordinate units)")
        .def_property_readonly("t", [](const PyMatchPoint &p)
                               { return p.t < 0 ? std::nullopt : std::optional<double>(p.t); }, "Timestamp of the matched point (if inputs have time and network has speed)")
        .def_property_readonly("speed", [](const PyMatchPoint &p)
                               { return p.speed < 0 ? std::nullopt : std::optional<double>(p.speed); }, "Speed at this matched point (if network edges have speed values)")
        .def_readonly("edge_offset", &PyMatchPoint::edge_offset, "Distance from the start of the edge to this point (coordinate units)")
        .def_readonly("cumulative_distance", &PyMatchPoint::cumulative_distance, "Total distance from the trajectory start to this point (coordinate units)")
        .def("__repr__", [](const PyMatchPoint &p)
             { return "<P x=" + fmt::format("{:.1f}", p.x) +
                      " y=" + fmt::format("{:.1f}", p.y) +
                      " d=" + fmt::format("{:.1f}", p.d) +
                      fmt::format(" t={}", p.t >= 0 ? fmt::format("{:.1f}", p.t) : "null") +
                      " speed=" + (p.speed >= 0 ? fmt::format("{:.1f}", p.speed) : "null") +
                      " edge_offset=" + fmt::format("{:.1f}", p.edge_offset) +
                      " cumulative_distance=" + fmt::format("{:.1f}", p.cumulative_distance) + ">"; });

    // PyMatchSegmentEdge struct
    py::class_<PyMatchSegmentEdge>(m, "MatchSegmentEdge", R"pbdoc(
        A matched road edge with interpolated points along the matched path.

        Contains the edge ID and a sequence of matched points representing where
        the trajectory intersects or follows this edge.
    )pbdoc")
        .def_readonly("edge_id", &PyMatchSegmentEdge::edge_id,
                      "ID of the matched road edge from the network")
        .def_readonly("points", &PyMatchSegmentEdge::points,
                      "List of PyMatchPoint objects along this edge, ordered by position")
        .def_readonly("reversed", &PyMatchSegmentEdge::reversed,
                      "True if geometry is reversed (GPS moved backward on same edge due to reverse_tolerance)")
        .def("__repr__", [](const PyMatchSegmentEdge &e)
             { return "<Edge edge_id=" + std::to_string(e.edge_id) + " with " + std::to_string(e.points.size()) + " points" + (e.reversed ? " (reversed)" : "") + ">"; });

    // PyMatchCandidate struct
    py::class_<PyMatchCandidate>(m, "MatchCandidate", R"pbdoc(
        A candidate match location for a GPS observation point.

        Represents a potential location on the road network where a GPS point might
        actually be located, accounting for GPS error. Multiple candidates per point
        are considered during matching.
    )pbdoc")
        .def_readonly("x", &PyMatchCandidate::x, "X coordinate of the snapped candidate location")
        .def_readonly("y", &PyMatchCandidate::y, "Y coordinate of the snapped candidate location")
        .def_property_readonly("t", [](const PyMatchCandidate &c)
                               { return c.t < 0 ? std::nullopt : std::optional<double>(c.t); }, "Timestamp of the original GPS observation (if trajectory has time)")
        .def_readonly("perpendicular_distance_to_matched_geometry", &PyMatchCandidate::perpendicular_distance_to_matched_geometry, "Perpendicular distance from GPS point to the matched edge geometry")
        .def_readonly("offset_from_start_of_edge", &PyMatchCandidate::offset_from_start_of_edge, "Distance from the start of the candidate edge to this match location")
        .def_readonly("trajectory_index", &PyMatchCandidate::trajectory_index, "1-based trajectory point index for this matched candidate")
        .def("__repr__", [](const PyMatchCandidate &c)
             { return "<Candidate x=" + fmt::format("{:.1f}", c.x) +
                      " y=" + fmt::format("{:.1f}", c.y) +
                      " t=" + (c.t < 0 ? "null" : fmt::format("{:.1f}", c.t)) +
                      " perpendicular_distance_to_matched_geometry=" + fmt::format("{:.1f}", c.perpendicular_distance_to_matched_geometry) +
                  " offset_from_start_of_edge=" + fmt::format("{:.1f}", c.offset_from_start_of_edge) +
                  " trajectory_index=" + std::to_string(c.trajectory_index) + ">"; });

    // PyMatchSegment struct
    py::class_<PyMatchSegment>(m, "MatchSegment", R"pbdoc(
        A continuous matched path segment between two GPS observation points.

        Represents the matched route from one GPS point to the next, potentially
        spanning multiple road edges. Each segment contains the start/end candidates
        and the sequence of edges traversed.
    )pbdoc")
        .def_readonly("p0", &PyMatchSegment::p0,
                      "Starting candidate point of this segment")
        .def_readonly("p1", &PyMatchSegment::p1,
                      "Ending candidate point of this segment")
        .def_readonly("edges", &PyMatchSegment::edges,
                      "List of PyMatchSegmentEdge objects forming the path from p0 to p1")
        .def("__repr__", [](const PyMatchSegment &s)
             { return "<Segment from (" + fmt::format("{:.1f}", s.p0.x) + ", " + fmt::format("{:.1f}", s.p0.y) + ", " + (s.p0.t < 0 ? "null" : fmt::format("{:.1f}", s.p0.t)) +
                      ") to (" + fmt::format("{:.1f}", s.p1.x) + ", " + fmt::format("{:.1f}", s.p1.y) + ", " + (s.p1.t < 0 ? "null" : fmt::format("{:.1f}", s.p1.t)) +
                      ") with " + std::to_string(s.edges.size()) + " edges>"; });

    // SubTrajectory struct
    py::class_<PySubTrajectory>(m, "SubTrajectory", R"pbdoc(
        A continuous portion of a trajectory that was matched or failed.

        Represents a successful match with segments. Failed portions are simply
        excluded from the results - only successfully matched sub-trajectories
        are returned in the SplitMatchResult.
    )pbdoc")
        .def_readonly("start_index", &PySubTrajectory::start_index,
                      "Starting trajectory point index (inclusive)")
        .def_readonly("end_index", &PySubTrajectory::end_index,
                      "Ending trajectory point index (inclusive)")
        .def_readonly("error_code", &PySubTrajectory::error_code,
                      "MatchErrorCode: SUCCESS if matched, or failure reason (informational)")
        .def_readonly("segments", &PySubTrajectory::segments,
                      "List of MatchSegment objects (only populated if error_code == SUCCESS)")
        .def("__repr__", [](const PySubTrajectory &s)
             {
                 std::string status = (s.error_code == MatchErrorCode::SUCCESS) ? "SUCCESS" : "FAILED";
                 return "<SubTraj [" + std::to_string(s.start_index) + "-" +
                        std::to_string(s.end_index) + "] " + status +
                        " with " + std::to_string(s.segments.size()) + " segments>"; });

    // SplitMatchResult struct
    py::class_<PySplitMatchResult>(m, "SplitMatchResult", R"pbdoc(
        Result of matching with automatic trajectory splitting.

        Contains a list of sub-trajectories representing all continuous matched
        portions and failed sections of the input trajectory. Each sub-trajectory
        indicates which points it covers and whether matching succeeded or failed.
    )pbdoc")
        .def_readonly("subtrajectories", &PySplitMatchResult::subtrajectories,
                      "List of SubTrajectory objects (both successful and failed portions)")
        .def("__repr__", [](const PySplitMatchResult &r)
             {
                 int success_count = 0;
                 int failed_count = 0;
                 for (const auto &sub : r.subtrajectories) {
                     if (sub.error_code == MatchErrorCode::SUCCESS) {
                         success_count++;
                     } else {
                         failed_count++;
                     }
                 }
                 return "<SplitMatch total_subs=" + std::to_string(r.subtrajectories.size()) +
                        " success=" + std::to_string(success_count) +
                        " failed=" + std::to_string(failed_count) + ">"; });

    // Trajectory struct
    py::class_<Trajectory>(m, "Trajectory", R"pbdoc(
        A GPS trajectory consisting of sequential observations.

        Trajectories can include timestamps (x, y, t) for time interpolation or be
        spatial-only (x, y). The trajectory's geometry and timestamps are used during
        map matching to find the best road path.
    )pbdoc")
        .def("__len__", [](const Trajectory &self)
             { return self.geom.get_num_points(); }, "Number of GPS observation points in the trajectory")
        .def("__repr__", [](const Trajectory &self)
             { return "<Trajectory num_points=" + std::to_string(self.geom.get_num_points()) +
                      (self.has_timestamps() ? " with timestamps" : " no timestamps") + ">"; })
        .def("has_timestamps", &Trajectory::has_timestamps,
             R"pbdoc(
            Check if the trajectory has timestamps.

            Returns:
                bool: True if timestamps are present, False otherwise
        )pbdoc")
        .def("to_xyt_tuples", [](const Trajectory &self)
             {
            py::list tuples;
            auto data = self.to_xyt_tuples();
            for (const auto &item : data) {
                tuples.append(py::make_tuple(std::get<0>(item), std::get<1>(item), std::get<2>(item)));
            }
            return tuples; }, R"pbdoc(
            Export trajectory as a list of (x, y, t) tuples - only if the original had timestamps.

            Returns:
                List of tuples, each containing (x_coord, y_coord, timestamp)
        )pbdoc")
        .def("to_xy_tuples", [](const Trajectory &self)
             {
            py::list tuples;
            auto data = self.to_xy_tuples();
            for (const auto &item : data) {
                tuples.append(py::make_tuple(std::get<0>(item), std::get<1>(item)));
            }
            return tuples; }, R"pbdoc(
            Export trajectory as a list of (x, y) tuples (spatial only).

            Returns:
                List of tuples, each containing (x_coord, y_coord)
        )pbdoc")
        .def_static("from_xyt_tuples", [](py::list tuples)
                    {
                std::vector<std::tuple<double, double, double>> data;
                for (auto item : tuples) {
                    if ((py::isinstance<py::tuple>(item) || py::isinstance<py::list>(item)) && py::len(item) == 3) {
                        py::sequence seq = item.cast<py::sequence>();
                        double x = py::float_(seq[0]);
                        double y = py::float_(seq[1]);
                        double t = py::float_(seq[2]);
                        data.emplace_back(x, y, t);
                    } else {
                        throw std::runtime_error("Each item must be a 3-element tuple or list (x, y, t)");
                    }
                }
                return Trajectory::from_xyt_tuples(data); }, py::arg("tuples"), R"pbdoc(
            Create a Trajectory from GPS points with timestamps.

            Args:
                tuples: List of (x, y, t) tuples/lists representing GPS observations with time

            Returns:
                Trajectory instance with timestamps for time interpolation
        )pbdoc")
        .def_static("from_xy_tuples", [](py::list tuples)
                    {
                std::vector<std::tuple<double, double>> data;
                for (auto item : tuples) {
                    if ((py::isinstance<py::tuple>(item) || py::isinstance<py::list>(item)) && py::len(item) == 2) {
                        py::sequence seq = item.cast<py::sequence>();
                        double x = py::float_(seq[0]);
                        double y = py::float_(seq[1]);
                        data.emplace_back(x, y);
                    } else {
                        throw std::runtime_error("Each item must be a 2-element tuple or list (x, y)");
                    }
                }
                return Trajectory::from_xy_tuples(data); }, py::arg("tuples"), R"pbdoc(
            Create a Trajectory from GPS points without timestamps (spatial-only).

            Args:
                tuples: List of (x, y) tuples/lists representing GPS observation locations

            Returns:
                Trajectory instance without time information
        )pbdoc");

    // UBODT class - Upper-Bounded Origin-Destination Table.
    // Held by std::shared_ptr because FastMapMatch and UBODT::read_ubodt return shared_ptr.
    py::class_<UBODT, std::shared_ptr<UBODT>>(m, "UBODT", R"pbdoc(
        Upper-Bounded Origin-Destination Table used by FastMapMatch.

        A precomputed routing table that stores shortest (or fastest) paths
        up to a cost upper bound (delta) between all node pairs in the network.
        Generate one with UBODTGenAlgorithm.generate_ubodt() and load with
        UBODT.read_ubodt(). Pass an instance to FastMapMatch to skip the
        cache directory entirely.
    )pbdoc")
        .def_static("read_ubodt",
                    [](const std::string &filename, int progress_step)
                    { return UBODT::read_ubodt(filename, progress_step); },
                    py::arg("filename"),
                    py::arg("progress_step") = 100000,
                    R"pbdoc(
            Read a UBODT from a binary file produced by UBODTGenAlgorithm.

            Args:
                filename: Path to the .bin UBODT file.
                progress_step: Print progress every N rows (0 disables, default 100000).

            Returns:
                A UBODT instance.
        )pbdoc")
        .def_property_readonly("delta", &UBODT::get_delta,
                               "Upper bound (delta) the UBODT was generated with")
        .def_property_readonly("network_hash", &UBODT::get_network_hash,
                               "Hash of the network this UBODT was generated from (empty for legacy files)")
        .def_property_readonly("mode", &UBODT::get_mode,
                               "TransitionMode this UBODT was generated with")
        .def_property_readonly("num_vertices", &UBODT::get_num_vertices,
                               "Number of vertices used for perfect hashing")
        .def_property_readonly("num_rows", &UBODT::get_num_rows,
                               "Number of records stored in the table");

    // UBODTGenAlgorithm class - generates a UBODT for a given network and mode.
    py::class_<UBODTGenAlgorithm>(m, "UBODTGenAlgorithm", R"pbdoc(
        Generator for Upper-Bounded Origin-Destination Tables.

        Precomputes a UBODT for a given Network/NetworkGraph and writes it to a
        binary file. The resulting file can be loaded with UBODT.read_ubodt().
    )pbdoc")
        .def(py::init<const Network &, const NetworkGraph &, TransitionMode>(),
             py::arg("network"),
             py::arg("graph"),
             py::arg("mode"),
             py::keep_alive<1, 2>(),
             py::keep_alive<1, 3>(),
             R"pbdoc(
            Create a UBODTGenAlgorithm.

            Args:
                network: Finalized road network.
                graph: NetworkGraph built from the same network and mode.
                mode: TransitionMode.SHORTEST or TransitionMode.FASTEST.
        )pbdoc")
        .def("generate_ubodt", &UBODTGenAlgorithm::generate_ubodt,
             py::arg("filename"),
             py::arg("delta"),
             py::arg("network_hash") = std::string(""),
             R"pbdoc(
            Generate the UBODT and write it to a binary file.

            Args:
                filename: Output file path.
                delta: Cost upper bound (distance for SHORTEST, time for FASTEST).
                network_hash: Optional network hash embedded in the file for validation.
        )pbdoc")
        .def("precompute_ubodt_omp", &UBODTGenAlgorithm::precompute_ubodt_omp,
             py::arg("filename"),
             py::arg("delta"),
             py::arg("network_hash"),
             R"pbdoc(
            Generate the UBODT in parallel using OpenMP and write it to a binary file.

            Args:
                filename: Output file path.
                delta: Cost upper bound (distance for SHORTEST, time for FASTEST).
                network_hash: Network hash embedded in the file for validation.
        )pbdoc");

    // FastMapMatch class
    py::class_<FastMapMatch>(m, "FastMapMatch", R"pbdoc(
        Fast map matching algorithm using Hidden Markov Model with UBODT optimization.

        Matches GPS trajectories to a road network by finding the most probable sequence
        of road edges, considering both emission probabilities (GPS accuracy) and transition
        probabilities (path likelihood). Uses precomputed UBODT for fast path lookups.
    )pbdoc")
        // Accept cache_dir as pathlib.Path (or any object with __fspath__)
        .def(
            py::init([](const Network &network, TransitionMode mode, std::optional<double> max_distance_between_candidates, std::optional<double> max_time_between_candidates, py::object cache_dir)
                     {
                // Accept str or Path-like (with __fspath__)
                std::string cache_dir_str;
                if (py::isinstance<py::str>(cache_dir)) {
                    cache_dir_str = cache_dir.cast<std::string>();
                } else if (py::hasattr(cache_dir, "__fspath__")) {
                    cache_dir_str = py::str(cache_dir.attr("__fspath__")());
                } else {
                    throw std::invalid_argument("cache_dir must be a str or Path-like object");
                }
                return new FastMapMatch(network, mode, max_distance_between_candidates, max_time_between_candidates, cache_dir_str); }),
            py::arg("network"),
            py::arg("mode"),
            py::arg("max_distance_between_candidates") = std::nullopt,
            py::arg("max_time_between_candidates") = std::nullopt,
            py::arg("cache_dir") = "./ubodt_cache",
            R"pbdoc(
            Create a FastMapMatch instance with automatic UBODT management.

            Args:
                network: Road network with spatial index built (call finalize() first)
                mode: Routing mode (TransitionMode.SHORTEST for distance, FASTEST for time)
                max_distance_between_candidates: Maximum distance in meters (for SHORTEST mode)
                max_time_between_candidates: Maximum time in seconds (for FASTEST mode)
                cache_dir: Directory for caching UBODT files (str or Path, default: "./ubodt_cache")

            Note:
                Only the relevant parameter is used depending on mode. This constructor automatically generates/loads UBODT from cache based
                on network hash, mode, and delta. UBODT is cached for reuse.
                Accepts str or pathlib.Path for cache_dir.
        )pbdoc")
        .def(py::init<const Network &, TransitionMode, std::shared_ptr<UBODT>>(),
             py::arg("network"),
             py::arg("mode"),
             py::arg("ubodt"),
             R"pbdoc(
            Create a FastMapMatch instance from a pre-loaded UBODT.

            Use this when you want to manage UBODT lifecycle yourself - e.g. share
            one UBODT across multiple FastMapMatch instances, load from a custom
            path, or avoid the on-disk cache directory entirely.

            Args:
                network: Road network with spatial index built (call finalize() first).
                mode: Routing mode - must match the mode the UBODT was generated with.
                ubodt: A UBODT instance (typically from UBODT.read_ubodt(...)).

            Raises:
                RuntimeError: If the UBODT's network hash, mode, or vertex count
                    does not match the provided network.
        )pbdoc")
        .def("match", &FastMapMatch::pymatch_trajectory,
             py::arg("trajectory"),
             py::arg("max_candidates") = 8,
             py::arg("candidate_search_radius"),
             py::arg("gps_error"),
             py::arg("reverse_tolerance") = 0.0,
             py::arg("reference_speed") = std::nullopt,
             py::return_value_policy::move,
             R"pbdoc(
            Match a GPS trajectory with automatic splitting on failures.

            This method performs candidate search once and reuses it when matching
            sub-trajectories. When the matching algorithm
            encounters failures (no candidates, disconnected layers), it automatically
            continues matching from the next viable point instead of stopping.

            Args:
                trajectory: Trajectory with GPS observations (with or without timestamps)

                max_candidates: Maximum number of candidate edges to consider per GPS point.
                   Higher values improve matching quality in dense networks but slow
                   performance. Typical: 4-16. Default: 8.

                candidate_search_radius: Maximum distance to search for candidate edges
                   around each GPS point (in coordinate units). Should exceed typical GPS
                   errors. Typical: 30-100 meters. Default: 50.

                gps_error: Expected GPS accuracy (standard deviation in coordinate units).
                   Used in emission probability: P(obs|candidate) ~ exp(-dist²/(2*gps_error²)).
                   Higher values are more tolerant of GPS noise. Typical: 10-100 meters.
                   Default: 50.

                reverse_tolerance: Maximum distance allowed when routing backward along
                   an edge (in coordinate units). Set to 0 to forbid reversing, which is
                   recommended for directed road networks. Default: 0.0.

                transition_mode: Routing cost metric - SHORTEST (distance) or FASTEST (time).
                   Must match the mode used to create NetworkGraph and UBODT. Default: SHORTEST.

                reference_speed: Expected travel speed for straight-line movement between GPS
                   points (distance units per time unit). REQUIRED for FASTEST mode, unused
                   for SHORTEST mode. This represents the typical speed at which vehicles
                   travel directly between points. Lower values encourage sticking to routes,
                   higher values allow more detours. Typical: average vehicle speed like 40-60
                   in urban areas. Default: None.

            Returns:
                SplitMatchResult: containing a list of sub-trajectories, each marked
                as either successfully matched (with segments) or failed (with error code)

            Example:
                Trajectory with points [0,1,2,3,4,5,6,7] where point 4 has no candidates:
                - Returns 2 sub-trajectories: [0-3] SUCCESS, [4-4] CANDIDATES_NOT_FOUND
                - If points 5-7 can be matched, adds [5-7] SUCCESS
                - Much faster than calling match_trajectory() multiple times since
                  candidate lookup is done once

            Note:
                The config's transition_mode must match the mode used to create the
                NetworkGraph and UBODT. For FASTEST mode, ensure reference_speed is set.
        )pbdoc");
}
