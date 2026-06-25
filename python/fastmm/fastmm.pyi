"""
Fast Map Matching (FASTMM) Python bindings via pybind11
"""
# Hand-maintained type stubs. Update by hand when bindings change.
from __future__ import annotations
import typing
__all__: list[str] = ['CANDIDATES_NOT_FOUND', 'DISCONNECTED_LAYERS', 'FASTEST', 'FastMapMatch', 'INDEX_OUT_OF_BOUNDS', 'INDEX_OUT_OF_BOUNDS_END', 'MatchCandidate', 'MatchErrorCode', 'MatchPoint', 'MatchSegment', 'MatchSegmentEdge', 'Network', 'NetworkGraph', 'SHORTEST', 'SUCCESS', 'SplitMatchResult', 'SubTrajectory', 'Trajectory', 'TransitionMode', 'UNKNOWN_ERROR']
class FastMapMatch:
    """
    
            Fast map matching algorithm using Hidden Markov Model with UBODT optimization.
    
            Matches GPS trajectories to a road network by finding the most probable sequence
            of road edges, considering both emission probabilities (GPS accuracy) and transition
            probabilities (path likelihood). Uses precomputed UBODT for fast path lookups.
        
    """
    def __init__(self, network: Network, mode: TransitionMode, max_distance_between_candidates: float | None = None, max_time_between_candidates: float | None = None, cache_dir: typing.Any = './ubodt_cache') -> None:
        """
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
        """
    def match(self, trajectory: Trajectory, max_candidates: int = 8, *, candidate_search_radius: float, gps_error: float, reverse_tolerance: float = 0.0, reference_speed: float | None = None, max_route_distance_factor: float = 0.0, turn_penalty_factor: float = 0.0) -> SplitMatchResult:
        """
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
        """
class MatchCandidate:
    """
    
            A candidate match location for a GPS observation point.
    
            Represents a potential location on the road network where a GPS point might
            actually be located, accounting for GPS error. Multiple candidates per point
            are considered during matching.
        
    """
    def __repr__(self) -> str:
        ...
    @property
    def offset_from_start_of_edge(self) -> float:
        """
        Distance from the start of the candidate edge to this match location
        """
    @property
    def perpendicular_distance_to_matched_geometry(self) -> float:
        """
        Perpendicular distance from GPS point to the matched edge geometry
        """
    @property
    def t(self) -> float | None:
        """
        Timestamp of the original GPS observation (if trajectory has time)
        """
    @property
    def trajectory_index(self) -> int:
        """
        1-based trajectory point index for this matched candidate
        """
    @property
    def x(self) -> float:
        """
        X coordinate of the snapped candidate location
        """
    @property
    def y(self) -> float:
        """
        Y coordinate of the snapped candidate location
        """
class MatchErrorCode:
    """
    Members:
    
      SUCCESS : Matching succeeded
    
      CANDIDATES_NOT_FOUND : No candidate edges found for trajectory
    
      DISCONNECTED_LAYERS : Trajectory has disconnected layers
    
      INDEX_OUT_OF_BOUNDS : Start edge index out of bounds
    
      INDEX_OUT_OF_BOUNDS_END : End edge index out of bounds
    
      UNKNOWN_ERROR : Unknown error occurred
    """
    CANDIDATES_NOT_FOUND: typing.ClassVar[MatchErrorCode]  # value = <MatchErrorCode.CANDIDATES_NOT_FOUND: 1>
    DISCONNECTED_LAYERS: typing.ClassVar[MatchErrorCode]  # value = <MatchErrorCode.DISCONNECTED_LAYERS: 2>
    INDEX_OUT_OF_BOUNDS: typing.ClassVar[MatchErrorCode]  # value = <MatchErrorCode.INDEX_OUT_OF_BOUNDS: 3>
    INDEX_OUT_OF_BOUNDS_END: typing.ClassVar[MatchErrorCode]  # value = <MatchErrorCode.INDEX_OUT_OF_BOUNDS_END: 4>
    SUCCESS: typing.ClassVar[MatchErrorCode]  # value = <MatchErrorCode.SUCCESS: 0>
    UNKNOWN_ERROR: typing.ClassVar[MatchErrorCode]  # value = <MatchErrorCode.UNKNOWN_ERROR: 255>
    __members__: typing.ClassVar[dict[str, MatchErrorCode]]  # value = {'SUCCESS': <MatchErrorCode.SUCCESS: 0>, 'CANDIDATES_NOT_FOUND': <MatchErrorCode.CANDIDATES_NOT_FOUND: 1>, 'DISCONNECTED_LAYERS': <MatchErrorCode.DISCONNECTED_LAYERS: 2>, 'INDEX_OUT_OF_BOUNDS': <MatchErrorCode.INDEX_OUT_OF_BOUNDS: 3>, 'INDEX_OUT_OF_BOUNDS_END': <MatchErrorCode.INDEX_OUT_OF_BOUNDS_END: 4>, 'UNKNOWN_ERROR': <MatchErrorCode.UNKNOWN_ERROR: 255>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: int) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: int) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class MatchPoint:
    """
    
            A matched point along a road edge in the map matching result.
    
            Represents a specific location on a matched edge, including its position
            relative to the edge start and the cumulative distance from the trajectory start.
        
    """
    def __repr__(self) -> str:
        ...
    @property
    def cumulative_distance(self) -> float:
        """
        Total distance from the trajectory start to this point (coordinate units)
        """
    @property
    def d(self) -> float:
        """
        Distance from the previous matched point along the edge (coordinate units)
        """
    @property
    def edge_offset(self) -> float:
        """
        Distance from the start of the edge to this point (coordinate units)
        """
    @property
    def speed(self) -> float | None:
        """
        Speed at this matched point (if network edges have speed values)
        """
    @property
    def t(self) -> float | None:
        """
        Timestamp of the matched point (if inputs have time and network has speed)
        """
    @property
    def x(self) -> float:
        """
        X coordinate of the matched point
        """
    @property
    def y(self) -> float:
        """
        Y coordinate of the matched point
        """
class MatchSegment:
    """
    
            A continuous matched path segment between two GPS observation points.
    
            Represents the matched route from one GPS point to the next, potentially
            spanning multiple road edges. Each segment contains the start/end candidates
            and the sequence of edges traversed.
        
    """
    def __repr__(self) -> str:
        ...
    @property
    def edges(self) -> list[MatchSegmentEdge]:
        """
        List of PyMatchSegmentEdge objects forming the path from p0 to p1
        """
    @property
    def p0(self) -> MatchCandidate:
        """
        Starting candidate point of this segment
        """
    @property
    def p1(self) -> MatchCandidate:
        """
        Ending candidate point of this segment
        """
class MatchSegmentEdge:
    """
    
            A matched road edge with interpolated points along the matched path.
    
            Contains the edge ID and a sequence of matched points representing where
            the trajectory intersects or follows this edge.
        
    """
    def __repr__(self) -> str:
        ...
    @property
    def edge_id(self) -> int:
        """
        ID of the matched road edge from the network
        """
    @property
    def points(self) -> list[MatchPoint]:
        """
        List of PyMatchPoint objects along this edge, ordered by position
        """
    @property
    def reversed(self) -> bool:
        """
        True if geometry is reversed (GPS moved backward on same edge due to reverse_tolerance)
        """
class Network:
    """
    
            A road network consisting of nodes (junctions) and directed edges (road segments).
    
            The network must be fully constructed (all edges added) before building the spatial
            index. Once the index is built, the network is ready for map matching operations.
    
            Example:
                >>> network = fastmm.Network()
                >>> network.add_edge(1, source=10, target=20, geom=[(0, 0), (100, 0)], speed=50.0)
                >>> network.finalize()
        
    """
    def __init__(self) -> None:
        """
                    Create an empty network.
        
                    Use add_edge() to populate the network with road segments, then call
                    finalize() to prepare it for map matching.
        """
    def add_edge(self, edge_id: int, source: int, target: int, geom: list, speed: float | None = None) -> None:
        """
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
        """
    def compute_hash(self) -> str:
        """
                    Compute a hash of the network structure for cache validation.
        
                    The hash is computed from edge count, sampled edge IDs, sources,
                    targets, and speeds. It's used to detect network changes and
                    invalidate cached UBODT files.
        
                    Returns:
                        str: 8-character hexadecimal hash string
        """
    def finalize(self) -> None:
        """
                    Build the spatial R-tree index for efficient candidate edge lookup.
        
                    This MUST be called after adding all edges and before creating a NetworkGraph
                    or performing any map matching operations. The index enables fast spatial
                    queries to find nearby road segments for GPS points.
        
                    Raises:
                        RuntimeError: If called on an empty network
        """
    def get_edge_count(self) -> int:
        """
                    Get the total number of edges in the network.
        
                    Returns:
                        int: Number of edges
        """
    def get_node_count(self) -> int:
        """
                    Get the total number of nodes in the network.
        
                    Returns:
                        int: Number of unique nodes
        """
class NetworkGraph:
    """
    
            A routing graph built from a Network for path finding.
    
            The graph representation depends on the chosen routing mode:
            - SHORTEST: Uses edge distances for path costs
            - FASTEST: Uses edge travel times (distance/speed) for path costs
    
            A separate graph must be created for each routing mode, and FASTEST mode
            requires that all edges have speed values defined.
        
    """
    def __init__(self, network: Network, mode: TransitionMode = ...) -> None:
        """
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
        """
class SplitMatchResult:
    """
    
            Result of matching with automatic trajectory splitting.
    
            Contains a list of sub-trajectories representing all continuous matched
            portions and failed sections of the input trajectory. Each sub-trajectory
            indicates which points it covers and whether matching succeeded or failed.
        
    """
    def __repr__(self) -> str:
        ...
    @property
    def subtrajectories(self) -> list[SubTrajectory]:
        """
        List of SubTrajectory objects (both successful and failed portions)
        """
class SubTrajectory:
    """
    
            A continuous portion of a trajectory that was matched or failed.
    
            Represents a successful match with segments. Failed portions are simply
            excluded from the results - only successfully matched sub-trajectories
            are returned in the SplitMatchResult.
        
    """
    def __repr__(self) -> str:
        ...
    @property
    def end_index(self) -> int:
        """
        Ending trajectory point index (inclusive)
        """
    @property
    def error_code(self) -> MatchErrorCode:
        """
        MatchErrorCode: SUCCESS if matched, or failure reason (informational)
        """
    @property
    def segments(self) -> list[MatchSegment]:
        """
        List of MatchSegment objects (only populated if error_code == SUCCESS)
        """
    @property
    def start_index(self) -> int:
        """
        Starting trajectory point index (inclusive)
        """
class Trajectory:
    """
    
            A GPS trajectory consisting of sequential observations.
    
            Trajectories can include timestamps (x, y, t) for time interpolation or be
            spatial-only (x, y). The trajectory's geometry and timestamps are used during
            map matching to find the best road path.
        
    """
    @staticmethod
    def from_xy_tuples(tuples: list) -> Trajectory:
        """
                    Create a Trajectory from GPS points without timestamps (spatial-only).
        
                    Args:
                        tuples: List of (x, y) tuples/lists representing GPS observation locations
        
                    Returns:
                        Trajectory instance without time information
        """
    @staticmethod
    def from_xyt_tuples(tuples: list) -> Trajectory:
        """
                    Create a Trajectory from GPS points with timestamps.
        
                    Args:
                        tuples: List of (x, y, t) tuples/lists representing GPS observations with time
        
                    Returns:
                        Trajectory instance with timestamps for time interpolation
        """
    def __len__(self) -> int:
        """
        Number of GPS observation points in the trajectory
        """
    def __repr__(self) -> str:
        ...
    def has_timestamps(self) -> bool:
        """
                    Check if the trajectory has timestamps.
        
                    Returns:
                        bool: True if timestamps are present, False otherwise
        """
    def to_xy_tuples(self) -> list:
        """
                    Export trajectory as a list of (x, y) tuples (spatial only).
        
                    Returns:
                        List of tuples, each containing (x_coord, y_coord)
        """
    def to_xyt_tuples(self) -> list:
        """
                    Export trajectory as a list of (x, y, t) tuples - only if the original had timestamps.
        
                    Returns:
                        List of tuples, each containing (x_coord, y_coord, timestamp)
        """
class TransitionMode:
    """
    Members:
    
      SHORTEST : Distance-based routing
    
      FASTEST : Time-based routing
    """
    FASTEST: typing.ClassVar[TransitionMode]  # value = <TransitionMode.FASTEST: 1>
    SHORTEST: typing.ClassVar[TransitionMode]  # value = <TransitionMode.SHORTEST: 0>
    __members__: typing.ClassVar[dict[str, TransitionMode]]  # value = {'SHORTEST': <TransitionMode.SHORTEST: 0>, 'FASTEST': <TransitionMode.FASTEST: 1>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: int) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: int) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
CANDIDATES_NOT_FOUND: MatchErrorCode  # value = <MatchErrorCode.CANDIDATES_NOT_FOUND: 1>
DISCONNECTED_LAYERS: MatchErrorCode  # value = <MatchErrorCode.DISCONNECTED_LAYERS: 2>
FASTEST: TransitionMode  # value = <TransitionMode.FASTEST: 1>
INDEX_OUT_OF_BOUNDS: MatchErrorCode  # value = <MatchErrorCode.INDEX_OUT_OF_BOUNDS: 3>
INDEX_OUT_OF_BOUNDS_END: MatchErrorCode  # value = <MatchErrorCode.INDEX_OUT_OF_BOUNDS_END: 4>
SHORTEST: TransitionMode  # value = <TransitionMode.SHORTEST: 0>
SUCCESS: MatchErrorCode  # value = <MatchErrorCode.SUCCESS: 0>
UNKNOWN_ERROR: MatchErrorCode  # value = <MatchErrorCode.UNKNOWN_ERROR: 255>
