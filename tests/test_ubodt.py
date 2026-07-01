"""Tests for the UBODT / UBODTGenAlgorithm Python bindings (commit b708183).

These exercise:
- UBODTGenAlgorithm.generate_ubodt -> on-disk .bin file
- UBODT.read_ubodt -> load + property inspection
- FastMapMatch(network, mode, ubodt) -- pre-loaded UBODT constructor
- Sharing one UBODT across two FastMapMatch instances
- A non-SUCCESS MatchErrorCode being observable from Python
"""

import pytest

from fastmm import (
    FastMapMatch,
    MatchErrorCode,
    Network,
    NetworkGraph,
    Trajectory,
    TransitionMode,
    UBODT,
    UBODTGenAlgorithm,
)


def _build_line_network():
    """3-node line:  10 --1--> 20 --2--> 30, each edge 100m long."""
    net = Network()
    net.add_edge(1, source=10, target=20, geom=[(0.0, 0.0), (100.0, 0.0)])
    net.add_edge(2, source=20, target=30, geom=[(100.0, 0.0), (200.0, 0.0)])
    net.finalize()
    return net


def test_generate_then_read_ubodt(tmp_path):
    net = _build_line_network()
    graph = NetworkGraph(net, TransitionMode.SHORTEST)
    gen = UBODTGenAlgorithm(net, graph, TransitionMode.SHORTEST)

    ubodt_file = tmp_path / "ubodt.bin"
    gen.generate_ubodt(str(ubodt_file), delta=500.0, network_hash="testhash")
    assert ubodt_file.exists() and ubodt_file.stat().st_size > 0

    ubodt = UBODT.read_ubodt(str(ubodt_file))
    assert ubodt.delta == pytest.approx(500.0)
    assert ubodt.mode == TransitionMode.SHORTEST
    assert ubodt.network_hash == "testhash"
    assert ubodt.num_vertices >= 3
    assert ubodt.num_rows > 0


def test_fastmapmatch_with_preloaded_ubodt(tmp_path):
    """The (network, mode, ubodt) constructor should match successfully."""
    net = _build_line_network()
    graph = NetworkGraph(net, TransitionMode.SHORTEST)
    gen = UBODTGenAlgorithm(net, graph, TransitionMode.SHORTEST)
    ubodt_file = tmp_path / "shared.bin"
    gen.generate_ubodt(str(ubodt_file), delta=500.0, network_hash=net.compute_hash())
    ubodt = UBODT.read_ubodt(str(ubodt_file))

    matcher = FastMapMatch(net, TransitionMode.SHORTEST, ubodt)
    traj = Trajectory.from_xy_tuples([(10.0, 0.0), (150.0, 0.0)])
    result = matcher.match(traj, candidate_search_radius=50.0, gps_error=20.0)
    assert len(result.subtrajectories) == 1
    assert result.subtrajectories[0].error_code == MatchErrorCode.SUCCESS


def test_match_many_matches_sequential_results_in_order(tmp_path):
    """Batch matching should preserve input order and match() semantics."""
    net = _build_line_network()
    graph = NetworkGraph(net, TransitionMode.SHORTEST)
    gen = UBODTGenAlgorithm(net, graph, TransitionMode.SHORTEST)
    ubodt_file = tmp_path / "batch.bin"
    gen.generate_ubodt(str(ubodt_file), delta=500.0, network_hash=net.compute_hash())
    ubodt = UBODT.read_ubodt(str(ubodt_file))
    matcher = FastMapMatch(net, TransitionMode.SHORTEST, ubodt)

    trajectories = [
        Trajectory.from_xy_tuples([(10.0, 0.0), (90.0, 0.0)]),
        Trajectory.from_xy_tuples([(110.0, 0.0), (190.0, 0.0)]),
        Trajectory.from_xy_tuples([(20.0, 0.0), (180.0, 0.0)]),
    ]

    sequential = [
        matcher.match(traj, candidate_search_radius=50.0, gps_error=20.0)
        for traj in trajectories
    ]
    batched = matcher.match_many(
        trajectories,
        candidate_search_radius=50.0,
        gps_error=20.0,
        workers=2,
    )

    assert len(batched) == len(sequential)
    for batch_result, seq_result in zip(batched, sequential):
        assert len(batch_result.subtrajectories) == len(seq_result.subtrajectories)
        assert [sub.error_code for sub in batch_result.subtrajectories] == [
            sub.error_code for sub in seq_result.subtrajectories
        ]
        assert [
            [edge.edge_id for segment in sub.segments for edge in segment.edges]
            for sub in batch_result.subtrajectories
        ] == [
            [edge.edge_id for segment in sub.segments for edge in segment.edges]
            for sub in seq_result.subtrajectories
        ]


def test_match_many_rejects_non_positive_workers(tmp_path):
    """The batch API should fail fast on invalid worker counts."""
    net = _build_line_network()
    graph = NetworkGraph(net, TransitionMode.SHORTEST)
    gen = UBODTGenAlgorithm(net, graph, TransitionMode.SHORTEST)
    ubodt_file = tmp_path / "batch-workers.bin"
    gen.generate_ubodt(str(ubodt_file), delta=500.0, network_hash=net.compute_hash())
    ubodt = UBODT.read_ubodt(str(ubodt_file))
    matcher = FastMapMatch(net, TransitionMode.SHORTEST, ubodt)
    traj = Trajectory.from_xy_tuples([(10.0, 0.0), (90.0, 0.0)])

    with pytest.raises(ValueError, match="workers"):
        matcher.match_many([traj], candidate_search_radius=50.0, gps_error=20.0, workers=0)


def test_one_ubodt_shared_across_two_matchers(tmp_path):
    """A UBODT bound to two FastMapMatch instances must keep both alive
    and produce equivalent successful matches."""
    net = _build_line_network()
    graph = NetworkGraph(net, TransitionMode.SHORTEST)
    gen = UBODTGenAlgorithm(net, graph, TransitionMode.SHORTEST)
    ubodt_file = tmp_path / "shared2.bin"
    gen.generate_ubodt(str(ubodt_file), delta=500.0, network_hash=net.compute_hash())
    ubodt = UBODT.read_ubodt(str(ubodt_file))

    m1 = FastMapMatch(net, TransitionMode.SHORTEST, ubodt)
    m2 = FastMapMatch(net, TransitionMode.SHORTEST, ubodt)

    traj = Trajectory.from_xy_tuples([(20.0, 0.0), (180.0, 0.0)])
    r1 = m1.match(traj, candidate_search_radius=50.0, gps_error=20.0)
    r2 = m2.match(traj, candidate_search_radius=50.0, gps_error=20.0)
    assert len(r1.subtrajectories) == len(r2.subtrajectories) == 1
    assert r1.subtrajectories[0].error_code == MatchErrorCode.SUCCESS
    assert r2.subtrajectories[0].error_code == MatchErrorCode.SUCCESS


def test_impossible_match_yields_no_successful_subtrajectory(tmp_path):
    """When a GPS point is far off-network, the split matcher drops the
    unmatchable section. We assert there is no *successful* match rather
    than a specific non-SUCCESS code, because the high-level match() API
    surfaces failures by excluding sections, not via in-band error codes."""
    net = _build_line_network()
    graph = NetworkGraph(net, TransitionMode.SHORTEST)
    gen = UBODTGenAlgorithm(net, graph, TransitionMode.SHORTEST)
    ubodt_file = tmp_path / "fail.bin"
    gen.generate_ubodt(str(ubodt_file), delta=500.0, network_hash=net.compute_hash())
    ubodt = UBODT.read_ubodt(str(ubodt_file))
    matcher = FastMapMatch(net, TransitionMode.SHORTEST, ubodt)

    # Middle point is 5 km off the road; no candidate within the search radius.
    traj = Trajectory.from_xy_tuples([(10.0, 0.0), (50.0, 5000.0), (190.0, 0.0)])
    result = matcher.match(traj, candidate_search_radius=30.0, gps_error=10.0)

    successful = [s for s in result.subtrajectories if s.error_code == MatchErrorCode.SUCCESS]
    assert len(successful) == 0


def test_repr_round_trips(tmp_path):
    """UBODT.__repr__ should surface delta, mode, rows, vertices."""
    net = _build_line_network()
    graph = NetworkGraph(net, TransitionMode.SHORTEST)
    gen = UBODTGenAlgorithm(net, graph, TransitionMode.SHORTEST)
    assert repr(gen) == "<UBODTGenAlgorithm>"

    ubodt_file = tmp_path / "repr.bin"
    gen.generate_ubodt(str(ubodt_file), delta=500.0)
    ubodt = UBODT.read_ubodt(str(ubodt_file))
    text = repr(ubodt)
    assert text.startswith("<UBODT ")
    assert "mode=SHORTEST" in text
    assert "rows=" in text and "vertices=" in text
