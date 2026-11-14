"""match_many_rows piece/break fields: n_pieces, piece_starts, n_pts_matched, gap_length.

cpath silently concatenates sub-trajectories that are not connected on the network; these
fields let callers see the breaks and bridge the metres cpath leaves out.
"""
import math

from fastmm import FastMapMatch, Network, Trajectory, TransitionMode

KW = dict(candidate_search_radius=50.0, gps_error=20.0, workers=1)


def _matcher(tmp_path):
    net = Network()
    net.add_edge(1, source=10, target=20, geom=[(0.0, 0.0), (100.0, 0.0)])
    net.add_edge(2, source=20, target=30, geom=[(100.0, 0.0), (200.0, 0.0)])
    net.add_edge(3, source=40, target=50, geom=[(0.0, 5000.0), (100.0, 5000.0)])
    net.finalize()
    return FastMapMatch(net, TransitionMode.SHORTEST, max_distance_between_candidates=1000.0, cache_dir=str(tmp_path))


def _rows(m, pts):
    t = Trajectory.from_xy_tuples(pts)
    (r,) = m.match_many_rows([t], [list(range(len(pts)))], [len(pts)], **KW)
    return r


def _chord(pts, i):
    return math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])


def test_single_piece_has_no_gap(tmp_path):
    r = _rows(_matcher(tmp_path), [(10.0, 1.0), (90.0, 1.0), (150.0, 1.0)])
    assert (r.n_pieces, r.piece_starts, r.n_pts, r.n_pts_matched, r.gap_length) == (1, "0", 3, 3, 0.0)
    assert r.match_status == "full"


def test_point_without_candidates_splits_and_counts_gap(tmp_path):
    pts = [(10.0, 1.0), (90.0, 1.0), (50.0, 900.0), (110.0, 1.0), (150.0, 1.0)]
    r = _rows(_matcher(tmp_path), pts)
    assert r.cpath == "1,2"
    assert (r.n_pieces, r.piece_starts, r.n_pts, r.n_pts_matched) == (2, "0,1", 5, 4)
    assert math.isclose(r.gap_length, _chord(pts, 1) + _chord(pts, 2), rel_tol=1e-9)
    assert r.match_status == "full"  # unchanged semantics: only successful subs are emitted


def test_disconnected_pieces(tmp_path):
    pts = [(10.0, 1.0), (90.0, 1.0), (10.0, 5001.0), (90.0, 5001.0)]
    r = _rows(_matcher(tmp_path), pts)
    assert r.cpath == "1,3"
    assert (r.n_pieces, r.piece_starts, r.n_pts_matched) == (2, "0,1", 4)
    assert math.isclose(r.gap_length, _chord(pts, 1), rel_tol=1e-9)


def test_piece_starting_on_previous_edge_shares_cpath_entry(tmp_path):
    pts = [(10.0, 1.0), (30.0, 1.0), (50.0, 900.0), (60.0, 1.0), (90.0, 1.0)]
    r = _rows(_matcher(tmp_path), pts)
    assert r.cpath == "1"
    assert (r.n_pieces, r.piece_starts) == (2, "0,0")


# ---- break causes (break_idx / break_type, SplitMatchResult.breaks) ----
from fastmm import BreakType  # noqa: E402


def _net(tmp_path, edges):
    net = Network()
    for eid, s, t, geom in edges:
        net.add_edge(eid, source=s, target=t, geom=geom)
    net.finalize()
    m = FastMapMatch(net, TransitionMode.SHORTEST, max_distance_between_candidates=1000.0, cache_dir=str(tmp_path))
    return m, net


def _breaks(m, pts, **kw):
    args = dict(KW, **kw)
    t = Trajectory.from_xy_tuples(pts)
    (r,) = m.match_many_rows([t], [list(range(len(pts)))], [len(pts)], **args)
    (res,) = m.match_many([t], **args)
    obj = [(b.index, b.type) for b in res.breaks]
    rows = list(zip([int(v) for v in r.break_idx.split(",") if v], [BreakType(int(v)) for v in r.break_type.split(",") if v]))
    assert obj == rows  # object API and rows agree
    return rows, res


def test_no_road_breaks(tmp_path):
    rows, _ = _breaks(_matcher(tmp_path), [(10.0, 1.0), (90.0, 1.0), (50.0, 900.0), (110.0, 1.0), (150.0, 1.0)])
    assert rows == [(1, BreakType.NO_ROAD), (2, BreakType.NO_ROAD)]


def test_unreachable_break(tmp_path):
    rows, _ = _breaks(_matcher(tmp_path), [(10.0, 1.0), (90.0, 1.0), (10.0, 5001.0), (90.0, 5001.0)])
    assert rows == [(1, BreakType.UNREACHABLE)]


def test_backward_jitter_break(tmp_path):
    m, _ = _net(tmp_path, [(1, 10, 20, [(0.0, 0.0), (100.0, 0.0)])])
    rows, _ = _breaks(m, [(10.0, 1.0), (50.0, 1.0), (44.0, 1.0), (90.0, 1.0)])
    assert rows == [(1, BreakType.BACKWARD)]


def test_wrong_way_on_one_way_break(tmp_path):
    m, _ = _net(tmp_path, [(1, 10, 20, [(0.0, 0.0), (100.0, 0.0)])])
    rows, _ = _breaks(m, [(90.0, 1.0), (40.0, 1.0)])
    assert rows == [(0, BreakType.ONEWAY_CONTRA)]


def test_two_way_road_is_not_contra(tmp_path):
    m, _ = _net(tmp_path, [(1, 10, 20, [(0.0, 0.0), (100.0, 0.0)]), (2, 20, 10, [(100.0, 0.0), (0.0, 0.0)])])
    rows, _ = _breaks(m, [(90.0, 1.0), (40.0, 1.0)])
    assert rows == []


def test_divided_road_crossing_break(tmp_path):
    # eastbound carriageway y=0, westbound twin y=30, not connected. Rider goes west on the eastbound one.
    m, _ = _net(tmp_path, [(1, 10, 20, [(0.0, 0.0), (100.0, 0.0)]), (2, 30, 40, [(100.0, 30.0), (0.0, 30.0)])])
    rows, _ = _breaks(m, [(80.0, 12.0), (40.0, 2.0)], candidate_search_radius=25.0)
    assert rows == [(0, BreakType.DIVIDED_CONTRA)]


def test_detour_break_reports_route_ratio(tmp_path):
    # Two parallel eastbound roads 58 apart, joined only by a long loop to the south and west.
    m, _ = _net(tmp_path, [(10, 1, 2, [(0.0, 0.0), (100.0, 0.0)]), (11, 3, 4, [(0.0, 60.0), (100.0, 60.0)]),
                           (12, 2, 5, [(100.0, 0.0), (100.0, -200.0)]), (13, 5, 6, [(100.0, -200.0), (-100.0, -200.0)]),
                           (14, 6, 7, [(-100.0, -200.0), (-100.0, 60.0)]), (15, 7, 3, [(-100.0, 60.0), (0.0, 60.0)])])
    pts = [(40.0, 1.0), (45.0, 59.0)]
    rows, res = _breaks(m, pts, max_route_distance_factor=2.0)
    assert rows == [(0, BreakType.DETOUR)]
    # shortest legal route: rest of edge 10 (60) + loop (200 + 200 + 260) + all of edge 15 (100, b snaps to its end)
    want = (60.0 + 200.0 + 200.0 + 260.0 + 100.0) / math.hypot(5.0, 58.0)
    assert math.isclose(res.breaks[0].route_ratio, want, rel_tol=1e-6)


# ---- grouping breaks into map issues ----
from types import SimpleNamespace  # noqa: E402

from fastmm.map_issues import map_issues  # noqa: E402


def _row(idx, typ):
    return SimpleNamespace(break_idx=",".join(map(str, idx)), break_type=",".join(str(int(t)) for t in typ))


def test_map_issues_groups_trips_by_place_type_and_direction():
    west = [(0.00100, 0.0001), (0.00060, 0.0001)]  # same spot, heading west
    east = list(reversed(west))
    trajs = [west, west, west, east, [(0.0101, 0.0101), (0.0102, 0.0101)], west]
    rows = [_row([0], [BreakType.ONEWAY_CONTRA])] * 3 + [_row([0], [BreakType.ONEWAY_CONTRA]),
                                                         _row([0], [BreakType.NO_ROAD]), _row([], [])]
    issues = map_issues(trajs, rows, trip_ids=list("abcdef"), cell=0.002, min_trips=3)
    assert len(issues) == 1
    (it,) = issues
    assert (it["issue"], it["n_trips"], it["trips"]) == ("oneway_direction", 3, ["a", "b", "c"])
    assert it["n_passing"] == 5  # a, b, c, the eastbound d and the unbroken f pass through the cell
    assert math.isclose(it["share"], 3 / 5)
    assert abs(it["heading_deg"] - 180.0) < 1e-6


def test_map_issues_skips_backward_jitter():
    trajs = [[(0.0, 0.0), (0.0001, 0.0)]] * 4
    assert map_issues(trajs, [_row([0], [BreakType.BACKWARD])] * 4, min_trips=1) == []


def test_next_segment_of_same_road_is_not_a_twin(tmp_path):
    # One-way eastbound road in two segments; a westbound rider near the junction sees the next segment,
    # which is attached to the contra edge and is not the other carriageway.
    m, _ = _net(tmp_path, [(1, 10, 20, [(0.0, 0.0), (100.0, 0.0)]), (2, 20, 30, [(100.0, 0.0), (200.0, 0.0)]),
                           (3, 40, 20, [(100.0, 100.0), (100.0, 0.0)])])
    rows, _ = _breaks(m, [(110.0, 1.0), (40.0, 1.0)])
    assert rows == [(0, BreakType.ONEWAY_CONTRA)]
