# fastmm

fastmm is a fast (C++) map-matching library for python with no dependencies, and the ability to interpolate time on the match (not just position), and also match as much as possible of the GPS trace (not just fail if a single point is wonky).

It's based on a desire to map match a lot of vehicle trace data quickly, without the infrastructure to spin up OSRM / Valhalla. (And this is probably faster as there's no IPC ... ?)

It is based on <https://github.com/cyang-kth/fmm> but updated to:

- Include Python helper classes for automatic trajectory splitting and time interpolation for the match.
- Remove GDAL/OGR dependencies - networks are created programmatically from Python
- Be buildable on Windows/Linux/Mac with modern tooling
- Focus on Python packaging with distributable wheels
- Remove STMatch - we'll focus on FMM for now
- Automated windows, linux, and macOS wheel builds

## TODO

- currently if a point from GPS trace fails (too far etc.) it's excluded from match. Should add it to match just with start == end and appropriate error code.
- test the time apportioning.
- If not found in UBODT, instead of bailing, do a normal djikstra lookup.
- max_distance_between_candidates is not a hard limit in UBODT ... I think. Test this, and if needed, add an extra check.
- Specify versions for build libs (e.g. cibuildwheel).

## Installation

```bash
pip install fastmm
```

## Quick Start (Recommended)

The simplest way to use fastmm is with the high-level `FastMapMatch` class:

```python
from pathlib import Path
from fastmm import FastMapMatch, MatchErrorCode, Network, Trajectory, TransitionMode

# Create and populate network
network = Network()
# Edge IDs and Node IDs are long long (64-bit) for OSM compatibility
network.add_edge(1234567890123, source=10, target=20, geom=[[0, 0], [100, 0]])
network.finalize()

# Create matcher with automatic UBODT caching (SHORTEST mode - distance-based)
matcher = FastMapMatch(
    network,
    TransitionMode.SHORTEST,
    max_distance_between_candidates=300.0,
    cache_dir=Path("./ubodt_cache")
)

# Match a trajectory of (x, y, t)
trajectory = Trajectory.from_xy_tuples([(10, 0, 1), (50, 0, 2), (150, 0, 3)])
result = matcher.match(
    trajectory,
    max_candidates=8,
    candidate_search_radius=50,
    gps_error=50
)

# Construct the points:
matched_xyt = []
for sub in result.subtrajectories:
    if sub.error_code == MatchErrorCode.SUCCESS:
        for segment in sub.segments:
            for edge in segment.edges:
                for p in edge.points:
                    matched_xyt.append((p.x, p.y, p.t))
```

For time-based routing you simply add a speed on all edges, and use `TransitionMode.FASTEST`. Otherwise it's the same.

## Automatic Trajectory Splitting

For trajectories that might have gaps or failures, `match()` automatically filters out troublesome sections, and matches everything it can. That is:
- It ignores points with no nearby road candidates (e.g., in tunnels, off-network) - it just returns the map matched sections either side of the erroneous point. (You can choose to merge them yourself later if you want.)
- If there's a break in the matching due to e.g. a very long distance between two points (data issues, teleportation etc.) then again, it'll return the map matches sections either side of this gap.

## Interpolating time

If your trajectory has timestamps, you often want your resulting match to include time as well i.e. show you at what speed/time your vehicle moved along the matched geometry. This library returns this. If your network doesn't have speed, it just apportions the time along the matched geometry between two GPS linearly. If you have speed, it uses this correctly e.g. if the match segment concludes a 100km/hr edge and a 50km/hr of equal length, it'll apportion less time on the faster edge than on the slower edge.

## Understanding Delta Parameters

The `delta` parameter (called `max_distance_between_candidates` or `max_time_between_candidates` in `MapMatcher`) controls the maximum routing cost for precomputed paths in the UBODT table:

### SHORTEST Mode (Distance-Based)
- **Units**: Same as your network (typically meters)
- **Meaning**: Maximum road network distance between GPS points
- **Recommendation**: 2-3x your expected maximum distance between consecutive GPS points
- **Example**: If GPS points are ~100m apart, use delta=300m

### FASTEST Mode (Time-Based)
- **Units**: Seconds
- **Meaning**: Maximum travel time between GPS points
- **Recommendation**: 2-3x your expected maximum travel time between GPS points
- **Example**: For 200m spacing at 50km/h expected speed: 200m ÷ (50,000m/3600s) ≈ 14.4s → use delta=40s

**Trade-offs**:
- **Larger delta**: Better matching quality (more routing options), but larger file size and slower generation
- **Smaller delta**: Faster generation and smaller files, but may fail to find paths between distant GPS points

## Understanding Reverse Tolerance

The `reverse_tolerance` parameter handles GPS measurement noise that causes slight backward movement on the **same edge**. This is even though we're operating on *directed* edges already - it's to account for a bit of GPS error etc. e.g. if the GPS goes backward. Without this, our routing would work - it'd just end up matching to the other side of the road, which would mean the route would go to the end of the road then back down to the current position (but on the other edge) ... which means if they're stationary and the GPS is jumping, it could look like the vehicle is traveling up and down the street a lot.

### How It Works

**Edge Traversal:** The graph uses **directed edges**. Dijkstra routing always respects edge direction (source → target). For OSM data with bidirectional roads, you should have two edges (one per direction).

**Same-Edge Positioning:** When two consecutive GPS points match to the **same edge** with the second point having a lower offset than the first (backward movement), `reverse_tolerance` controls whether this is allowed:

```python
# Example: GPS noise causes apparent backward movement
GPS Point 1 → Edge 1 at offset=80m (80% along A→B)
GPS Point 2 → Edge 1 at offset=50m (50% along A→B)

# Without reverse_tolerance (0.0):
# - Transition has infinite cost → rejected
# - Algorithm may match Point 2 to opposite-direction edge (creating fake U-turn)
# - Or Point 2 gets skipped in split mode

# With reverse_tolerance=40 (40m in these units):
# - Backward movement = 80m - 50m = 30m
# - 30m < 40m ✅ Allowed with cost=0
```

### The Reversed Flag

When backward movement is allowed (within tolerance), the `reversed` flag indicates this occurred. **The geometry is automatically corrected** to always go forward (from lower to higher offset), so you don't need to handle backward linestrings:

```python
for segment in result.segments:
    for edge in segment.edges:
        if edge.reversed:
            # Geometry has been auto-corrected to go forward
            # But you know GPS moved backward on this edge
            # May want to flag this for quality control
            print(f"Edge {edge.edge_id} had backward GPS movement (now corrected)")

        # All edges have forward geometry regardless of reversed flag
        # edge.points always go from lower to higher offset
        for point in edge.points:
            print(f"  Offset: {point.edge_offset}, Position: ({point.x}, {point.y})")
```

**What the `reversed` flag means:**
- `reversed=True`: GPS moved backward (offset1 > offset2), geometry was auto-corrected to go forward
- `reversed=False`: GPS moved forward normally (offset1 <= offset2)

**No special handling needed** - the geometry is always correct. Use the flag for:
- Quality control (detecting erratic GPS behavior)
- Statistics (counting backward movements)
- Debugging (understanding matching behavior)

### Recommendations

Start with `reverse_tolerance=0`. If you're seeing a lot of jumping around on stationary (ish) vehicles, either do some pre-filtering, or try e.g. `reverse_tolerance=20m`.

## Routing Modes: SHORTEST vs FASTEST

FastMM supports two routing modes that affect how map matching selects the most likely path.

In both cases, the emission probability is balanced with the transition probability. The emission probability is the likelihood a candidate is on the given edge based simply on how far away it is from the edge - the closer, the higher the probability. This can be controlled with the `gps_error` parameter - make it larger and the emission probability will stay higher even if the point is further away.

### SHORTEST Mode (Distance-based)

Uses distance as the routing metric. This is the default mode and matches trajectories based on spatial proximity. The transition probability is:

```
tp = min(euclidean_dist, path_dist) / max(euclidean_dist, path_dist)
```

This compares the straight-line distance between GPS points to the network path distance. Higher probability when the path closely follows the GPS trajectory.

If you find your routes are sticking to the nearest edge, regardless of the feasibility of the route, this is because your `gps_error` is likely too large (?). Likewise the converse.

### FASTEST Mode (Time-based)

Uses travel time as the routing metric. Requires speed values on all edges. The transition probability is

```
expected_time = euclidean_dist / reference_speed
actual_time = path_time (sum of segment_length / segment_speed)
tp = min(expected_time, actual_time) / max(expected_time, actual_time)
```

Similarly to above, this gives a higher priority when the travel time is the same, or faster than the expected travel time (which is the euclidean distance divided by the reference speed). If you're finding your routes are sticking to the nearest edge, regardless of the feasibility of the route, either decrease `gps_error` as above, or decrease (?) the reference speed.

## Developing

You can run the tests with:

```bash
pytest .
```

You can create stubs with:

```bash
python ./generate_stubs_for_wheel.py ./python/fastmm/ ./python/fastmm/
```

For now, this is better than doing it in a CI/CD pipeline as Windows is painful.