from fastmm import (  # noqa: F401
    Network,
    NetworkGraph,
    FastMapMatch,
    UBODT,
    UBODTGenAlgorithm,
    Trajectory,
    MatchSegment,
    MatchCandidate,
    MatchPoint,
    MatchSegmentEdge,
    SubTrajectory,
    SplitMatchResult,
    MatchErrorCode,
    TransitionMode,
)
from routingkit_ch import ContractionHierarchy, CHQuery, INF_WEIGHT  # noqa: F401

try:
    from fastmm._version import version as __version__
except ImportError:
    __version__ = "unknown"

__all__ = [
    # Map matching
    "Network",
    "NetworkGraph",
    "FastMapMatch",
    "UBODT",
    "UBODTGenAlgorithm",
    "Trajectory",
    "MatchSegment",
    "MatchCandidate",
    "MatchPoint",
    "MatchSegmentEdge",
    "SubTrajectory",
    "SplitMatchResult",
    "MatchErrorCode",
    "TransitionMode",
    # Contraction hierarchy
    "ContractionHierarchy",
    "CHQuery",
    "INF_WEIGHT",
]
