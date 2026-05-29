# fastmm - Fast Map Matching for Python

# Import C++ bindings
from .fastmm import *  # noqa: F401,F403

try:
    from ._version import version as __version__
except ImportError:
    __version__ = "unknown"

# C++ classes from bindings
__all__ = [
    # Core classes
    "Network",
    "NetworkGraph",
    "FastMapMatch",
    "UBODT",
    "UBODTGenAlgorithm",
    "Trajectory",
    # Result classes
    "MatchSegment",
    "MatchCandidate",
    "MatchPoint",
    "MatchSegmentEdge",
    "SubTrajectory",
    "SplitMatchResult",
    "MatchErrorCode",
    # Enums
    "TransitionMode",
]
