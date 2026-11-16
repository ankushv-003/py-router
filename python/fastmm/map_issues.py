"""Turn match breaks from many trips into map-issue candidates.

A single break says little: GPS noise, a rider on a footpath or a bad fix all break a match. When
many different trips break at the same place, in the same direction and for the same reason, the
map is the likely cause. ``map_issues`` groups the per-trip breaks reported by
``FastMapMatch.match_many_rows`` (``break_idx`` / ``break_type``) into grid cells and returns the
cells where enough distinct trips broke, with the share of trips through the cell that broke.

Break type -> what to check on the map:

=================  ===================================================================
``NO_ROAD``         road missing from the map (or riders cut through premises / off-road)
``ONEWAY_CONTRA``   one-way tagged in the wrong direction, or habitual wrong-way riding
``DIVIDED_CONTRA``  divided road crossed or ridden against flow: missing median opening
``DETOUR``          legal route far longer than the ride: missing turn, link or opening
``UNREACHABLE``     no legal route at all: missing connection or one-way trap
=================  ===================================================================

``BACKWARD`` breaks (GPS jitter on one edge) are matcher artefacts and are skipped.

Coordinates are in the network's units (planar degrees in production). Pure Python, no
dependencies beyond the extension module.
"""

from __future__ import annotations

import math
from collections import defaultdict
from typing import Optional, Sequence

from .fastmm import BreakType

MAP_BREAK_TYPES = {
    BreakType.NO_ROAD: "missing_road",
    BreakType.ONEWAY_CONTRA: "oneway_direction",
    BreakType.DIVIDED_CONTRA: "divided_road_crossing",
    BreakType.DETOUR: "missing_turn_or_link",
    BreakType.UNREACHABLE: "missing_connection",
}

# Types whose direction matters: a wrong-way one-way only flags trips going one way.
_DIRECTIONAL = {BreakType.ONEWAY_CONTRA, BreakType.DIVIDED_CONTRA, BreakType.DETOUR, BreakType.UNREACHABLE}


def _xy(traj) -> Sequence:
    return traj.to_xy_tuples() if hasattr(traj, "to_xy_tuples") else traj


def _ints(s: str):
    return [int(v) for v in s.split(",")] if s else []


def break_records(trajectories: Sequence, rows: Sequence, trip_ids: Optional[Sequence] = None):
    """One record per map-relevant break: dict(trip, type, x, y, heading_deg).

    trajectories: the trajectories passed to match_many_rows (Trajectory objects or (x, y) sequences).
    rows: the MatchRows returned for them. The location is the midpoint of the break interval and the
    heading is the direction of travel from point i to i+1 (degrees, 0 = +x, counter-clockwise).
    """
    if len(trajectories) != len(rows):
        raise ValueError("trajectories and rows must have the same length")
    out = []
    for k, (traj, r) in enumerate(zip(trajectories, rows)):
        idx, typ = _ints(r.break_idx), _ints(r.break_type)
        if not idx:
            continue
        pts = _xy(traj)
        trip = trip_ids[k] if trip_ids is not None else k
        for i, t in zip(idx, typ):
            bt = BreakType(t)
            if bt not in MAP_BREAK_TYPES or i + 1 >= len(pts):
                continue
            (x0, y0), (x1, y1) = pts[i][:2], pts[i + 1][:2]
            out.append(dict(trip=trip, type=bt, x=(x0 + x1) / 2, y=(y0 + y1) / 2,
                            heading_deg=math.degrees(math.atan2(y1 - y0, x1 - x0)) % 360.0))
    return out


def _sector(h: float, n: int = 8) -> int:
    return int(((h + 180.0 / n) % 360.0) // (360.0 / n))


def map_issues(trajectories: Sequence, rows: Sequence, trip_ids: Optional[Sequence] = None,
               cell: float = 0.0005, min_trips: int = 3, min_share: float = 0.0) -> list:
    """Group breaks over many trips into map-issue candidates, most-supported first.

    A candidate is a (grid cell, break type, travel direction) where at least ``min_trips`` distinct
    trips broke. Direction (8 sectors) is only used for directional types. ``share`` is the number of
    breaking trips divided by the trips with a point in the cell, so busy junctions with occasional
    noise rank below places where most riders break.

    Returns dicts: issue, type, x, y (mean break location), n_trips, n_passing, share,
    heading_deg (circular mean, directional types only), trips (sorted list of trip ids).
    """
    recs = break_records(trajectories, rows, trip_ids)
    groups = defaultdict(list)
    for r in recs:
        key = (math.floor(r["x"] / cell), math.floor(r["y"] / cell), r["type"],
               _sector(r["heading_deg"]) if r["type"] in _DIRECTIONAL else -1)
        groups[key].append(r)
    groups = {k: g for k, g in groups.items() if len({r["trip"] for r in g}) >= min_trips}
    if not groups:
        return []

    # Trips passing through each candidate cell (any input point in it).
    cells = {(k[0], k[1]) for k in groups}
    passing = defaultdict(set)
    for k, traj in enumerate(trajectories):
        trip = trip_ids[k] if trip_ids is not None else k
        for p in _xy(traj):
            c = (math.floor(p[0] / cell), math.floor(p[1] / cell))
            if c in cells:
                passing[c].add(trip)

    out = []
    for (cx, cy, bt, _), g in groups.items():
        trips = {r["trip"] for r in g}
        n_pass = max(len(passing[(cx, cy)] | trips), 1)
        share = len(trips) / n_pass
        if share < min_share:
            continue
        item = dict(issue=MAP_BREAK_TYPES[bt], type=bt, x=sum(r["x"] for r in g) / len(g),
                    y=sum(r["y"] for r in g) / len(g), n_trips=len(trips), n_passing=n_pass,
                    share=share, heading_deg=None, trips=sorted(trips, key=str))
        if bt in _DIRECTIONAL:
            sx = sum(math.cos(math.radians(r["heading_deg"])) for r in g)
            sy = sum(math.sin(math.radians(r["heading_deg"])) for r in g)
            item["heading_deg"] = math.degrees(math.atan2(sy, sx)) % 360.0
        out.append(item)
    out.sort(key=lambda d: (-d["n_trips"], -d["share"]))
    return out
