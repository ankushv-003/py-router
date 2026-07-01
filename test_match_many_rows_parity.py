"""Parity test: match_many_rows (native materialize) must be byte-identical to the
Python fold (_rows_from_result) run over match_many. Mirrors the ETL contract."""
import math
import random

from fastmm import FastMapMatch, MatchErrorCode, Network, TransitionMode
from fastmm.fastmm import Trajectory


def build_grid(n=12, step=100.0):
    """A simple 2-D grid road network. Edge IDs are deterministic."""
    net = Network()
    eid = 1
    node = lambda r, c: r * n + c
    for r in range(n):
        for c in range(n):
            if c + 1 < n:  # horizontal edge
                net.add_edge(eid, source=node(r, c), target=node(r, c + 1),
                             geom=[(c * step, r * step), ((c + 1) * step, r * step)])
                eid += 1
            if r + 1 < n:  # vertical edge
                net.add_edge(eid, source=node(r, c), target=node(r + 1, c),
                             geom=[(c * step, r * step), (c * step, (r + 1) * step)])
                eid += 1
    net.finalize()
    return net


# ── Reference Python fold — copied verbatim from ETL _rows_from_result ────────
# (minus snap_flag, which the caller derives; we compare the fields C++ returns)

def ref_rows(coords, orig_idx, result):
    cpath, lengths, durations = [], [], []
    opath = [-1] * len(coords)
    for sub in result.subtrajectories:
        if sub.error_code != MatchErrorCode.SUCCESS:
            continue
        for seg in sub.segments:
            c0 = seg.p0.trajectory_index - 1
            c1 = seg.p1.trajectory_index - 1
            first_edge_id = seg.edges[0].edge_id if seg.edges else -1
            last_edge_id = seg.edges[-1].edge_id if seg.edges else -1
            if 0 <= c0 < len(orig_idx):
                opath[orig_idx[c0]] = first_edge_id
            if 0 <= c1 < len(orig_idx):
                opath[orig_idx[c1]] = last_edge_id
            for e in seg.edges:
                pts = e.points
                if pts:
                    edge_len = pts[-1].cumulative_distance - pts[0].cumulative_distance
                    try:
                        edge_dur = float(pts[-1].t) - float(pts[0].t)
                    except (TypeError, AttributeError):
                        edge_dur = 0.0
                else:
                    edge_len = 0.0
                    edge_dur = 0.0
                if cpath and cpath[-1] == e.edge_id:
                    lengths[-1] += edge_len
                    durations[-1] += edge_dur
                else:
                    cpath.append(e.edge_id)
                    lengths.append(edge_len)
                    durations.append(edge_dur)
    n_success = sum(1 for s in result.subtrajectories if s.error_code == MatchErrorCode.SUCCESS)
    n_total = len(result.subtrajectories)
    status = 'failed' if n_success == 0 else ('partial' if n_success < n_total else 'full')
    n_reversed = sum(1 for sub in result.subtrajectories if sub.error_code == MatchErrorCode.SUCCESS
                     for seg in sub.segments for e in seg.edges if e.reversed)
    snap = [seg.p0.perpendicular_distance_to_matched_geometry
            for sub in result.subtrajectories if sub.error_code == MatchErrorCode.SUCCESS
            for seg in sub.segments] + \
           [seg.p1.perpendicular_distance_to_matched_geometry
            for sub in result.subtrajectories if sub.error_code == MatchErrorCode.SUCCESS
            for seg in sub.segments]
    max_snap = max(snap) if snap else 0.0
    return {
        'cpath': ",".join(map(str, cpath)),
        'opath': ",".join(map(str, opath)),
        'length': ",".join(f"{x:.6f}" for x in lengths),
        'duration': ",".join(f"{x:.3f}" for x in durations),
        'match_status': status,
        'n_sub': n_total,
        'n_reversed': n_reversed,
        'max_snap_dist_deg': max_snap,
    }


def test_match_many_rows_parity():
    net = build_grid(n=12, step=100.0)
    matcher = FastMapMatch(net, TransitionMode.SHORTEST,
                           max_distance_between_candidates=5000,
                           cache_dir=".cache/parity")

    rng = random.Random(42)
    trips, orig_idx_list, orig_len_list, coords_list = [], [], [], []
    for _ in range(300):
        # random walk along the grid with jitter; some short, some long
        n_pts = rng.randint(2, 15)
        x, y = rng.randint(0, 10) * 100.0, rng.randint(0, 10) * 100.0
        coords = []          # ORIGINAL pings (may include dropped ones)
        cleaned = []         # what actually goes to the matcher
        orig_idx = []        # cleaned[j] came from coords[orig_idx[j]]
        far_gap = rng.random() < 0.25  # 25% inject an off-network point -> partial/failed
        for k in range(n_pts):
            if far_gap and k == n_pts // 2:
                coords.append((5000.0 + rng.uniform(-8, 8), 5000.0))  # far from grid
            else:
                coords.append((x + rng.uniform(-8, 8), y + rng.uniform(-8, 8)))
            # simulate preclean dropping ~30% of points: they stay in coords (opath -1)
            # but are absent from the cleaned trajectory the matcher sees
            if rng.random() < 0.3 and len(coords) > 1:
                pass  # dropped: not appended to cleaned / orig_idx
            else:
                cleaned.append(coords[-1])
                orig_idx.append(k)
            if rng.random() < 0.5:
                x = min(1000.0, x + 100.0)
            else:
                y = min(1000.0, y + 100.0)
        if len(cleaned) < 2:      # matcher needs >=2 points
            cleaned = coords[:]
            orig_idx = list(range(len(coords)))
        xyt = [(lon, lat, float(orig_idx[j])) for j, (lon, lat) in enumerate(cleaned)]
        trips.append(Trajectory.from_xyt_tuples(xyt))
        orig_idx_list.append(orig_idx)
        orig_len_list.append(len(coords))
        coords_list.append(coords)

    params = dict(max_candidates=8, candidate_search_radius=30, gps_error=10)

    # Native materialized path
    rows = matcher.match_many_rows(trips, orig_idx_list, orig_len_list, workers=4, **params)
    # Python fold over match_many
    results = matcher.match_many(trips, workers=4, **params)

    assert len(rows) == len(results) == len(trips)
    mismatches = 0
    for i, (r, res, coords, oi) in enumerate(zip(rows, results, coords_list, orig_idx_list)):
        ref = ref_rows(coords, oi, res)
        got = {
            'cpath': r.cpath, 'opath': r.opath, 'length': r.length,
            'duration': r.duration, 'match_status': r.match_status,
            'n_sub': r.n_sub, 'n_reversed': r.n_reversed,
        }
        for k in ('cpath', 'opath', 'length', 'duration', 'match_status', 'n_sub', 'n_reversed'):
            if got[k] != ref[k]:
                mismatches += 1
                print(f"trip {i} field {k}:\n  native={got[k]!r}\n  python={ref[k]!r}")
        if not math.isclose(r.max_snap_dist_deg, ref['max_snap_dist_deg'], rel_tol=1e-9, abs_tol=1e-9):
            mismatches += 1
            print(f"trip {i} max_snap: native={r.max_snap_dist_deg} python={ref['max_snap_dist_deg']}")
    assert mismatches == 0, f"{mismatches} field mismatches"
    matched = sum(1 for r in rows if r.match_status != 'failed')
    print(f"OK: {len(trips)} trips, {matched} matched, 0 mismatches")


if __name__ == "__main__":
    test_match_many_rows_parity()
