# ch-router

`ch-router` is a fast (C++) Python package for **GPS map matching** and **road-network
routing**, with no Python runtime dependencies. It bundles two complementary engines:

- **Map matching** (`fastmm`) — snap noisy GPS traces onto a road network using a Hidden
  Markov Model with a precomputed UBODT. It can interpolate time along the match (not just
  position) and matches as much of the trace as possible instead of failing on a single bad
  point.
- **Contraction-hierarchy routing** (`routingkit_ch`) — build a contraction hierarchy from a
  weighted graph and answer point-to-point shortest-path queries in microseconds, via Python
  bindings over [RoutingKit](https://github.com/RoutingKit/RoutingKit).

Both are re-exported from the top-level `ch_router` package, so a single import gives you the
whole toolkit.

It's built for map matching a lot of vehicle trace data quickly, without the infrastructure to
spin up OSRM / Valhalla (and likely faster, since there's no IPC).

Source: [ankushv-003/ch-router](https://github.com/ankushv-003/ch-router). This is a fork of
[kodonnell/fastmm](https://github.com/kodonnell/fastmm), itself based on
<https://github.com/cyang-kth/fmm>, updated to:

- Add Python helper classes for automatic trajectory splitting and time interpolation.
- Remove the GDAL/OGR dependencies — networks are created programmatically from Python.
- Add RoutingKit contraction-hierarchy routing bindings.
- Focus on Python packaging with distributable wheels (Linux + macOS).
- Drop STMatch — the focus is FMM.

## Installation

```bash
pip install ch-router
```

Wheels are published for CPython 3.10–3.13 on Linux (manylinux_2_28) and macOS (11.0+).

## Map Matching

The high-level `FastMapMatch` class manages the UBODT for you:

```python
from pathlib import Path
from ch_router import FastMapMatch, MatchErrorCode, Network, Trajectory, TransitionMode

# Create and populate a network. Edge/node IDs are 64-bit for OSM compatibility.
# Geometry is a list of (x, y) tuples.
network = Network()
network.add_edge(1234567890123, source=10, target=20, geom=[(0, 0), (100, 0)])
network.add_edge(1234567890124, source=20, target=30, geom=[(100, 0), (200, 0)])
network.finalize()

# SHORTEST = distance-based. UBODT is generated/cached automatically.
matcher = FastMapMatch(
    network,
    TransitionMode.SHORTEST,
    max_distance_between_candidates=300.0,
    cache_dir=Path("./ubodt_cache"),
)

# A trajectory of (x, y, t) points. Use from_xy_tuples([(x, y), ...]) if you have no time.
trajectory = Trajectory.from_xyt_tuples([(10, 0, 1), (50, 0, 2), (150, 0, 3)])
result = matcher.match(
    trajectory,
    max_candidates=8,
    candidate_search_radius=50,
    gps_error=50,
)

# Collect the matched points:
matched_xyt = []
for sub in result.subtrajectories:
    if sub.error_code == MatchErrorCode.SUCCESS:
        for segment in sub.segments:
            for edge in segment.edges:
                for p in edge.points:
                    matched_xyt.append((p.x, p.y, p.t))
```

For time-based routing, set a `speed` on every edge and use `TransitionMode.FASTEST`. Otherwise
the API is identical.

### Sharing a UBODT across matchers (advanced)

`FastMapMatch`'s `cache_dir` constructor is the easy path. If you want to manage the
[UBODT](https://fmm-wiki.github.io/docs/documentation/fmm/#upper-bounded-origin-destination-table)
yourself — generate it once, load it, and share it across many matchers without touching the
disk cache — use `UBODTGenAlgorithm` and the pre-loaded constructor:

```python
from ch_router import (
    FastMapMatch, Network, NetworkGraph, TransitionMode, UBODT, UBODTGenAlgorithm,
)

network = Network()
network.add_edge(1, source=10, target=20, geom=[(0, 0), (100, 0)])
network.add_edge(2, source=20, target=30, geom=[(100, 0), (200, 0)])
network.finalize()

graph = NetworkGraph(network, TransitionMode.SHORTEST)
gen = UBODTGenAlgorithm(network, graph, TransitionMode.SHORTEST)

# IMPORTANT: embed the network hash so the pre-loaded constructor can validate it.
gen.generate_ubodt("ubodt.bin", delta=300.0, network_hash=network.compute_hash())

ubodt = UBODT.read_ubodt("ubodt.bin")
matcher = FastMapMatch(network, TransitionMode.SHORTEST, ubodt)  # shares this UBODT
```

If the UBODT's network hash, mode, or vertex count doesn't match the network, the constructor
raises `RuntimeError` — that's why you must pass `network_hash=network.compute_hash()` when
generating it for this path.

## Contraction-Hierarchy Routing

Build a contraction hierarchy from a directed, weighted graph (flat `uint32` arrays — NumPy
arrays or plain lists), then run point-to-point queries. Weights are unsigned integers.

```python
import numpy as np
from ch_router import ContractionHierarchy, CHQuery, INF_WEIGHT

# Graph: 0 -3-> 1 -4-> 2, plus a direct 0 -10-> 2.
tail   = np.array([0, 1, 0], dtype=np.uint32)
head   = np.array([1, 2, 2], dtype=np.uint32)
weight = np.array([3, 4, 10], dtype=np.uint32)

ch = ContractionHierarchy.build(3, tail, head, weight)   # 3 nodes
print(ch.node_count)                                     # 3

q = CHQuery(ch)
q.reset().add_source(0).add_target(2).run()
print(q.get_distance())    # 7  (via 0 -> 1 -> 2, cheaper than the direct arc of 10)
print(q.get_node_path())   # [0, 1, 2]
print(q.get_arc_path())    # [0, 1]  (indices into the tail/head/weight arrays)

# Unreachable targets return the sentinel INF_WEIGHT (2**31 - 1).
q.reset().add_source(2).add_target(0).run()
print(q.get_distance() == INF_WEIGHT)   # True
```

A `CHQuery` is reusable — call `reset()` between queries. The query keeps the underlying
`ContractionHierarchy` alive for its lifetime, so it is safe to let the `ch` variable go out of
scope while a query is in use.

### Persisting a contraction hierarchy

Building a CH on a large graph is expensive; persist it once and reload it:

```python
ch.save("graph.ch")                       # stable, portable CHB1 binary format
ch = ContractionHierarchy.load("graph.ch")
```

The `.ch` (CHB1) format is little-endian and version-tagged, so files are portable across
machines. Note it is **not** interchangeable with RoutingKit's own `save_file`/`load_file`
format.

## Automatic Trajectory Splitting

For traces with gaps or failures, `match()` filters out troublesome sections and matches
everything it can:

- Points with no nearby road candidate (tunnels, off-network) are skipped — you get the matched
  sections on either side. (Merge them yourself later if you want.)
- A break caused by a very long jump between two points (data issues, teleportation) likewise
  yields the matched sections on either side of the gap.

Because failures are dropped rather than reported in-band, the way to detect them is that the
affected section is **absent** from `result.subtrajectories` (or the list is empty when nothing
matched). Returned sub-trajectories carry `error_code == MatchErrorCode.SUCCESS`.

## Interpolating Time

If your trajectory has timestamps, the match includes time as well — i.e. at what speed/time the
vehicle moved along the matched geometry. Without per-edge speed, time is apportioned linearly
along the matched geometry between two GPS points. With per-edge speed, it's apportioned
correctly — e.g. between a 100 km/h edge and a 50 km/h edge of equal length, less time goes on
the faster edge.

## Understanding Delta Parameters

The `delta` parameter (`max_distance_between_candidates` or `max_time_between_candidates` on
`FastMapMatch`) controls the maximum routing cost for precomputed paths in the UBODT.

### SHORTEST Mode (distance-based)

- **Units**: same as your network (typically meters)
- **Meaning**: maximum road-network distance between GPS points
- **Recommendation**: 2–3× your expected maximum distance between consecutive GPS points
- **Example**: if GPS points are ~100 m apart, use `delta=300`

### FASTEST Mode (time-based)

- **Units**: seconds
- **Meaning**: maximum travel time between GPS points
- **Recommendation**: 2–3× your expected maximum travel time between GPS points
- **Example**: for 200 m spacing at 50 km/h: 200 ÷ (50000/3600) ≈ 14.4 s → use `delta=40`

**Trade-offs**:

- **Larger delta**: better matching quality (more routing options), but larger UBODT and slower
  generation. (Too small and `generate_ubodt` can produce an empty table that `read_ubodt`
  rejects.)
- **Smaller delta**: faster generation and smaller files, but may fail to connect distant GPS
  points.

## Understanding Reverse Tolerance

The `reverse_tolerance` parameter handles GPS noise that causes slight backward movement on the
**same edge**. We operate on *directed* edges, so without this a backward jiggle would route to
the end of the road and back down the opposite edge — making a stationary, jittery vehicle look
like it's driving up and down the street.

### How It Works

**Edge traversal:** the graph uses **directed edges**. Dijkstra routing always respects edge
direction (source → target). For OSM bidirectional roads, add two edges (one per direction).

**Same-edge positioning:** when two consecutive GPS points match the **same edge** with the
second at a lower offset (backward movement), `reverse_tolerance` decides whether it's allowed:

```python
# GPS noise causes apparent backward movement:
GPS Point 1 -> Edge 1 at offset = 80 m
GPS Point 2 -> Edge 1 at offset = 50 m

# reverse_tolerance = 0.0:  transition has infinite cost -> rejected
#   (algorithm may match Point 2 to the opposite-direction edge, a fake U-turn,
#    or skip Point 2 in split mode)
# reverse_tolerance = 40:   backward movement = 30 m < 40 m -> allowed at cost 0
```

### The Reversed Flag

When backward movement is allowed, the `reversed` flag records it. **Geometry is automatically
corrected** to always go forward (low → high offset), so you never handle backward linestrings:

```python
for segment in result.subtrajectories[0].segments:
    for edge in segment.edges:
        if edge.reversed:
            # Geometry already corrected to go forward; flag for QC if you like.
            print(f"Edge {edge.edge_id} had backward GPS movement (corrected)")
        for point in edge.points:
            print(f"  offset {point.edge_offset}: ({point.x}, {point.y})")
```

- `reversed=True`: GPS moved backward (offset1 > offset2), geometry auto-corrected forward.
- `reversed=False`: GPS moved forward normally (offset1 ≤ offset2).

No special handling is needed — geometry is always correct. Use the flag for quality control,
statistics, or debugging.

### Recommendations

Start with `reverse_tolerance=0`. If stationary-ish vehicles jump around, either pre-filter or
try e.g. `reverse_tolerance=20`.

## Routing Modes: SHORTEST vs FASTEST

Both modes balance the emission probability against the transition probability. The emission
probability is how likely a candidate is the right edge given its distance from the GPS point —
closer is higher. Tune it with `gps_error`: larger keeps emission high even for distant points.

### SHORTEST Mode (distance-based)

The default. Routes on distance, matching by spatial proximity. Transition probability:

```
tp = min(euclidean_dist, path_dist) / max(euclidean_dist, path_dist)
```

Higher when the network path closely follows the straight-line GPS distance. If routes stick to
the nearest edge regardless of feasibility, your `gps_error` is probably too large (and vice
versa).

### FASTEST Mode (time-based)

Routes on travel time; requires `speed` on all edges. Transition probability:

```
expected_time = euclidean_dist / reference_speed
actual_time   = sum(segment_length / segment_speed)
tp = min(expected_time, actual_time) / max(expected_time, actual_time)
```

Higher when travel time matches or beats the expected time (euclidean distance ÷ reference
speed). If routes stick to the nearest edge, decrease `gps_error` or the reference speed.

## Developing

Run the tests:

```bash
pytest .
```

Build and install from source (needs Boost and a C++17 compiler; OpenMP for parallel UBODT
generation):

```bash
pip install -e . --no-build-isolation
```

Type stubs (`.pyi`) for the compiled extensions are hand-maintained under `python/fastmm/` and
`python/routingkit_ch/`; update them by hand when the bindings change (each stub notes this in
its header).

## Roadmap / Known Limitations

- A failed GPS point (too far, etc.) is excluded from the match rather than emitted as a
  zero-length segment with an error code.
- If a path isn't found in the UBODT, fall back to a plain Dijkstra lookup instead of bailing.
- `max_distance_between_candidates` may not be a hard limit in the UBODT — needs verification and
  possibly an extra check.
- The contraction-hierarchy bindings currently expose point-to-point queries; many-to-many
  (`pin_targets`) and RoutingKit's native save/load format are not yet exposed.

## Third-Party Licenses

This package vendors source from the following projects under `third_party/`:

- **[RoutingKit](https://github.com/RoutingKit/RoutingKit)** — BSD-3-Clause. Source under
  `third_party/routingkit/` with the original `LICENSE` preserved and shipped inside the wheel
  (`routingkit_ch/LICENSE.RoutingKit`). Compiled into the `routingkit_ch._native` extension.
- **[spdlog](https://github.com/gabime/spdlog)** — MIT. Headers under `third_party/spdlog/`;
  used for logging in the C++ core.
- **[FiboHeap](https://github.com/beniz/fiboheap)** — **LGPL-3.0**. Header-only, under
  `third_party/fiboheap/` with its `LICENSE` preserved; used in the routing core. Note the LGPL
  terms when redistributing.

See each project's directory under `third_party/` for the full license text.
