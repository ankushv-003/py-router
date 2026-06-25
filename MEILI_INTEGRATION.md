# Integrating Meili capabilities into `fastmm`

## TL;DR — don't embed Valhalla; port its 3 transition-model features

The whole point of this package (per the README) is in-process, no-dependency, fast
map matching *without* spinning up OSRM/Valhalla. Embedding Meili (Valhalla's matcher)
would throw that away — tiles, IPC, a huge dep.

We don't need to. `fastmm` is **already an HMM matcher** (emission + transition +
Viterbi). Our investigation showed it loses to Meili on exactly **three transition-model
features Meili has and `fastmm` lacks**. Porting those into the existing Viterbi is small,
contained, and keeps the no-dependency design:

| Meili feature | what it fixes | `fastmm` status |
|---|---|---|
| `max_route_distance_factor` / `max_route_time_factor` | around-the-block detours, sparse-gap fills | **missing** (only a weak linear penalty) |
| `turn_penalty_factor` | divided-road / carriageway flapping (U-turns) | **missing** |
| sharper transition (beta) | general detour suppression | weak (linear ratio) |
| `interpolation_distance` (jitter dedup) | low-speed flapping | do in Python (helpers exist) |
| `breakage_distance` (split at gaps) | signal dropouts | Python trajectory-split helpers exist |

Validated impact: a Python post-hoc version of the plausibility gate cleaned the FMM
labels enough to lift **CH routing +1.5pp** on a holdout (see project memory). Moving it
*into* the transition model improves the matched path itself, not just downstream counts.

## Exactly where the code changes go

All in `src/mm/`:

### 1. `get_shortest_transition_probability` — `src/mm/transition_graph.cpp:37`
Today:
```cpp
return euclidean_distance >= path_distance ? 1.0 : euclidean_distance / path_distance;
```
This is a *linear* ratio — a 5×-detour still gets tp=0.2 (survives). Two changes:
- **Hard gate**: if `path_distance > max_route_distance_factor * euclidean_distance` → return 0 (reject). This is the lever that kills the carriageway/around-block detours.
- **Sharper falloff** (optional, Newson-Krumm): `exp(-(path_distance - euclidean_distance) / beta)` instead of the linear ratio.

### 2. Turn penalty — `src/mm/fmm/fmm_algorithm.cpp` `update_layer()`
The Viterbi step computes `tp` for each (candidate A → candidate B). Right there, add a
turn penalty using edge bearings (each `Edge` has `geom` + `length`, `type.hpp:50`):
```cpp
// outgoing bearing of A's edge near the match, incoming bearing of B's edge
double turn = angle_between(bearing_out(A), bearing_in(B));   // 0=straight, 180=U-turn
tp *= exp(-config.turn_penalty_factor * (turn/180.0));        // U-turn -> heavy penalty
```
A U-turn onto the opposing carriageway is ~180° → crushed. This is the carriageway-flap fix
Meili gets from `turn_penalty_factor` (its `auto` default is 200).

### 3. Config + bindings
- `FastMapMatchConfig` (`fmm_algorithm.hpp:42`, ctor `fmm_algorithm.cpp:200`): add
  `double max_route_distance_factor = 5.0;` and `double turn_penalty_factor = 0.0;`
  (defaults reproduce current behaviour → no regression for existing callers).
- `python/pybind11/fastmm_bindings.cpp`: expose the two new args on the match/config call,
  mirroring Meili's names so it's familiar.

### 4. Python layer (no C++ needed)
- `interpolation_distance`: dedup trajectory points closer than D before matching (we use
  15–25 m). Add to the existing `Trajectory`/split helpers.
- `breakage_distance`: the trajectory-split helpers already break long gaps — expose a
  meters threshold so we *break* instead of inventing road across a dropout.

## Why this is the right productionization

- **Keeps the package's identity**: still in-process, zero runtime deps, fast, our-graph
  edge IDs (so freq-corpus counts come for free — Valhalla would need edge re-mapping).
- **Small, contained diff**: ~2 functions + 2 config fields + bindings. No new third_party.
- **Reproduces the Meili quality win** (turn-penalty + plausibility gate were *the* levers;
  tightening `gps_error` alone did nothing in our sweep).
- **Backward compatible**: defaults = today's behaviour.

## Build / ship

```bash
pip install scikit-build-core pybind11        # build deps (see pyproject.toml)
pip install -e .                               # local editable build to test
# CI already builds manylinux/macos wheels (.github/) — bump version, tag, publish.
```

## Validation plan (before shipping)

1. Re-run the worst-trip bake-off (carriageway + gap cases) with the new params on vs off —
   expect length-ratio 7–10× → ~1× like Meili, with no `gps_error` change.
2. Re-run the corpus payoff test (baseline vs new-matcher labels → freq → CH on holdout) —
   confirm the +1.5pp holds / grows, now from a native-on-road path (not raw-bridge).
3. Parity check: with `turn_penalty_factor=0, max_route_distance_factor=∞`, output must be
   byte-identical to current `fastmm` (regression guard).
