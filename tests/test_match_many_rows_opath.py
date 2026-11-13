"""Regression: match_many_rows opath must give each point its OWN matched edge.

MatchCandidate.trajectory_index is 0-based, but materialize_rows used to subtract 1,
shifting opath one point left (point i got point i+1's edge, last point always -1).
"""

from fastmm import FastMapMatch, Network, Trajectory, TransitionMode


def _matcher(tmp_path):
    net = Network()
    net.add_edge(1, source=10, target=20, geom=[(0.0, 0.0), (100.0, 0.0)])
    net.add_edge(2, source=20, target=30, geom=[(100.0, 0.0), (200.0, 0.0)])
    net.add_edge(3, source=30, target=40, geom=[(200.0, 0.0), (300.0, 0.0)])
    net.finalize()
    return FastMapMatch(net, TransitionMode.SHORTEST, max_distance_between_candidates=1000.0,
                        cache_dir=str(tmp_path))


def test_opath_is_per_point(tmp_path):
    m = _matcher(tmp_path)
    t = Trajectory.from_xy_tuples([(10.0, 1.0), (90.0, 1.0), (150.0, 1.0), (250.0, 1.0)])
    (row,) = m.match_many_rows([t], [[0, 1, 2, 3]], [4], candidate_search_radius=50.0, gps_error=20.0, workers=1)
    assert row.opath == "1,1,2,3"
    assert row.cpath == "1,2,3"


def test_opath_remaps_through_orig_idx(tmp_path):
    # cleaned point k came from original ping orig_idx[k]; dropped original pings stay -1
    m = _matcher(tmp_path)
    t = Trajectory.from_xy_tuples([(10.0, 1.0), (150.0, 1.0), (250.0, 1.0)])
    (row,) = m.match_many_rows([t], [[0, 2, 4]], [5], candidate_search_radius=50.0, gps_error=20.0, workers=1)
    assert row.opath == "1,-1,2,-1,3"


def test_trajectory_index_is_zero_based(tmp_path):
    m = _matcher(tmp_path)
    t = Trajectory.from_xy_tuples([(10.0, 1.0), (150.0, 1.0), (250.0, 1.0)])
    (res,) = m.match_many([t], candidate_search_radius=50.0, gps_error=20.0, workers=1)
    (sub,) = res.subtrajectories
    idx = [s.p0.trajectory_index for s in sub.segments] + [sub.segments[-1].p1.trajectory_index]
    assert idx == [0, 1, 2]
