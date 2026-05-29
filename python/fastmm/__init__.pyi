# Hand-maintained type stubs. Update by hand when bindings change.
from __future__ import annotations
from fastmm.fastmm import FastMapMatch
from fastmm.fastmm import MatchCandidate
from fastmm.fastmm import MatchErrorCode
from fastmm.fastmm import MatchPoint
from fastmm.fastmm import MatchSegment
from fastmm.fastmm import MatchSegmentEdge
from fastmm.fastmm import Network
from fastmm.fastmm import NetworkGraph
from fastmm.fastmm import SplitMatchResult
from fastmm.fastmm import SubTrajectory
from fastmm.fastmm import Trajectory
from fastmm.fastmm import TransitionMode
from fastmm.fastmm import UBODT
from fastmm.fastmm import UBODTGenAlgorithm
from . import _version
from . import fastmm
__all__: list = ['Network', 'NetworkGraph', 'FastMapMatch', 'UBODT', 'UBODTGenAlgorithm', 'Trajectory', 'MatchSegment', 'MatchCandidate', 'MatchPoint', 'MatchSegmentEdge', 'SubTrajectory', 'SplitMatchResult', 'MatchErrorCode', 'TransitionMode']
CANDIDATES_NOT_FOUND: MatchErrorCode  # value = <MatchErrorCode.CANDIDATES_NOT_FOUND: 1>
DISCONNECTED_LAYERS: MatchErrorCode  # value = <MatchErrorCode.DISCONNECTED_LAYERS: 2>
FASTEST: TransitionMode  # value = <TransitionMode.FASTEST: 1>
INDEX_OUT_OF_BOUNDS: MatchErrorCode  # value = <MatchErrorCode.INDEX_OUT_OF_BOUNDS: 3>
INDEX_OUT_OF_BOUNDS_END: MatchErrorCode  # value = <MatchErrorCode.INDEX_OUT_OF_BOUNDS_END: 4>
SHORTEST: TransitionMode  # value = <TransitionMode.SHORTEST: 0>
SUCCESS: MatchErrorCode  # value = <MatchErrorCode.SUCCESS: 0>
UNKNOWN_ERROR: MatchErrorCode  # value = <MatchErrorCode.UNKNOWN_ERROR: 255>
__version__: str
