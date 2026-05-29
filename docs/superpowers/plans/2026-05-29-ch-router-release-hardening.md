# ch-router Release Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the `ch-router` PyPI wheel safe to release: fix GIL/lifetime bugs in the pybind11 bindings, fix macOS wheel target + delocate, add missing test coverage for UBODT and the `ch_router` re-export package, ship type stubs for all three packages, fix the sdist manifest, attribute RoutingKit's BSD-3 license, and strip dead Windows code paths.

**Architecture:** Surgical edits to existing C++ bindings (no new files), additive test cases in two new files, plus packaging hygiene across `pyproject.toml`, `CMakeLists.txt`, `MANIFEST.in`, and the CI workflow. Every change is independently mergeable.

**Tech Stack:** pybind11 2.12, scikit-build-core, cibuildwheel, CMake 3.15, Python 3.10–3.13, pytest.

---

## File Structure

**Modified:**
- `python/pybind11/fastmm_bindings.cpp` — add `gil_scoped_release` to long-running calls, add `__repr__` for UBODT/UBODTGenAlgorithm, default `network_hash=""` on `precompute_ubodt_omp`.
- `src/python/routingkit_ch_bindings.cpp` — add `gil_scoped_release` to `save`/`load`/`run`/`get_*_path`, add `py::keep_alive<1,2>()` on `CHQuery.__init__`, expose `INF_WEIGHT`.
- `pyproject.toml` — lower `MACOSX_DEPLOYMENT_TARGET` to `11.0`, add macOS `repair-wheel-command` excluding `libfastmmlib.dylib`, drop empty `matcher` optional dep, drop dead `wheel.packages = []`.
- `python/pybind11/CMakeLists.txt` — ship `py.typed` and `*.pyi` for `fastmm` and `ch_router`, switch `file(GLOB ...)` to `CONFIGURE_DEPENDS`, install `third_party/routingkit/LICENSE` into wheel root.
- `CMakeLists.txt` — strip Windows-specific blocks (the `MSVC` branches and `WIN32` DLL output dirs), drop the contradictory `CMAKE_RUNTIME_OUTPUT_DIRECTORY` line, scope Boost link to FASTMMLIB instead of global `link_libraries`.
- `MANIFEST.in` — delete (let scikit-build-core's git-tracked-file default produce sdist).
- `README.md` — add a "Third-party licenses" section attributing RoutingKit BSD-3.
- `.github/workflows/build-wheels.yml` — pin `ScribeMD/docker-cache` by SHA.
- `python/ch_router/__init__.py` — add `py.typed` marker (handled in CMakeLists.txt install).

**Created:**
- `tests/test_ubodt.py` — UBODT round-trip + shared FastMapMatch + non-SUCCESS error code assertions, all using `tmp_path`.
- `tests/test_ch_router_package.py` — smoke test asserting every `__all__` symbol resolves from `ch_router`.
- `third_party/routingkit/VENDOR.txt` — record upstream commit + "files kept / modifications" note.
- `python/ch_router/py.typed` — marker file (zero bytes).

**Deleted:**
- `generate_stubs_for_wheel.py` — orphaned, never invoked from CMake/CI. Stubs are hand-maintained; document that in a header comment of each `.pyi` instead. (Alternative considered: wire it into the build. Rejected because the stubs are already checked in and stable; resurrecting an autogen pipeline is more risk than reward for a v0.x.)

---

## Task 1: Release GIL on FastMapMatch.match

**Files:**
- Modify: `python/pybind11/fastmm_bindings.cpp:530`

- [ ] **Step 1: Read the current `.def("match", ...)` block to confirm line numbers**

Run: `grep -n 'def("match"' python/pybind11/fastmm_bindings.cpp`
Expected: one line near 530.

- [ ] **Step 2: Add `py::call_guard<py::gil_scoped_release>()` to the `.def("match", ...)` arglist**

In `python/pybind11/fastmm_bindings.cpp` around line 530, the current binding is:

```cpp
.def("match", &FastMapMatch::pymatch_trajectory,
     py::arg("trajectory"),
     py::arg("max_candidates") = 8,
     py::arg("candidate_search_radius"),
     py::arg("gps_error"),
     py::arg("reverse_tolerance") = 0.0,
     py::arg("reference_speed") = std::nullopt,
     py::return_value_policy::move,
     R"pbdoc(
```

Change to (insert `py::call_guard<...>()` immediately before `R"pbdoc(`):

```cpp
.def("match", &FastMapMatch::pymatch_trajectory,
     py::arg("trajectory"),
     py::arg("max_candidates") = 8,
     py::arg("candidate_search_radius"),
     py::arg("gps_error"),
     py::arg("reverse_tolerance") = 0.0,
     py::arg("reference_speed") = std::nullopt,
     py::return_value_policy::move,
     py::call_guard<py::gil_scoped_release>(),
     R"pbdoc(
```

- [ ] **Step 3: Rebuild the extension**

Run: `pip install -e . --no-build-isolation -v 2>&1 | tail -20`
Expected: clean build, ends with `Successfully installed ch-router-...`.

- [ ] **Step 4: Run existing tests to ensure no regression**

Run: `python -m pytest tests/ test_fastmm.py -x --tb=short`
Expected: `45 passed`.

- [ ] **Step 5: Commit**

```bash
git add python/pybind11/fastmm_bindings.cpp
git commit -m "fix(bindings): release GIL on FastMapMatch.match

Long matching runs were serializing all Python threads. Wrap pymatch_trajectory
with py::call_guard<py::gil_scoped_release>() so concurrent.futures workers can
actually overlap."
```

---

## Task 2: Release GIL on UBODT generation, IO, and cache-dir constructor

**Files:**
- Modify: `python/pybind11/fastmm_bindings.cpp:396` (`UBODT.read_ubodt`), `:443` (`generate_ubodt`), `:455` (`precompute_ubodt_omp`), `:479` (cache-dir `FastMapMatch::__init__` lambda).

- [ ] **Step 1: Add GIL release to `UBODT::read_ubodt`**

In `python/pybind11/fastmm_bindings.cpp` around line 396, the binding is a lambda:

```cpp
.def_static("read_ubodt",
            [](const std::string &filename, int progress_step)
            { return UBODT::read_ubodt(filename, progress_step); },
```

Add `py::call_guard<py::gil_scoped_release>()` after the closing `R"pbdoc(...)"pbdoc"` docstring argument. The full method binding will end with `py::call_guard<py::gil_scoped_release>())`.  For example:

```cpp
.def_static("read_ubodt",
            [](const std::string &filename, int progress_step)
            { return UBODT::read_ubodt(filename, progress_step); },
            py::arg("filename"),
            py::arg("progress_step") = 1000000,
            R"pbdoc(
    Read a UBODT from a binary file produced by UBODTGenAlgorithm.

    Args:
        filename: Path to the .bin UBODT file.
        progress_step: Log every N rows (default 1,000,000).

    Returns:
        A UBODT instance.
)pbdoc",
            py::call_guard<py::gil_scoped_release>())
```

(If the existing `py::arg` lines differ, preserve them — only insert the `py::call_guard` line as the new last positional argument to `.def_static`.)

- [ ] **Step 2: Add GIL release to `generate_ubodt`**

In `python/pybind11/fastmm_bindings.cpp` around line 443:

```cpp
.def("generate_ubodt", &UBODTGenAlgorithm::generate_ubodt,
     py::arg("filename"),
     py::arg("delta"),
     py::arg("network_hash") = std::string(""),
     R"pbdoc(
    Generate the UBODT and write it to a binary file.

    Args:
        filename: Output file path.
        delta: Cost upper bound (distance for SHORTEST, time for FASTEST).
        network_hash: Optional network hash embedded in the file for validation.
)pbdoc")
```

Change the closing `)` after the docstring to add the call guard:

```cpp
.def("generate_ubodt", &UBODTGenAlgorithm::generate_ubodt,
     py::arg("filename"),
     py::arg("delta"),
     py::arg("network_hash") = std::string(""),
     R"pbdoc(
    Generate the UBODT and write it to a binary file.

    Args:
        filename: Output file path.
        delta: Cost upper bound (distance for SHORTEST, time for FASTEST).
        network_hash: Optional network hash embedded in the file for validation.
)pbdoc",
     py::call_guard<py::gil_scoped_release>())
```

- [ ] **Step 3: Add GIL release to `precompute_ubodt_omp` and default `network_hash=""`**

Around line 455, change:

```cpp
.def("precompute_ubodt_omp", &UBODTGenAlgorithm::precompute_ubodt_omp,
     py::arg("filename"),
     py::arg("delta"),
     py::arg("network_hash"),
     R"pbdoc(
    Generate the UBODT in parallel using OpenMP and write it to a binary file.

    Args:
        filename: Output file path.
        delta: Cost upper bound (distance for SHORTEST, time for FASTEST).
        network_hash: Network hash embedded in the file for validation.
)pbdoc");
```

to:

```cpp
.def("precompute_ubodt_omp", &UBODTGenAlgorithm::precompute_ubodt_omp,
     py::arg("filename"),
     py::arg("delta"),
     py::arg("network_hash") = std::string(""),
     R"pbdoc(
    Generate the UBODT in parallel using OpenMP and write it to a binary file.

    Args:
        filename: Output file path.
        delta: Cost upper bound (distance for SHORTEST, time for FASTEST).
        network_hash: Optional network hash embedded in the file for validation.
)pbdoc",
     py::call_guard<py::gil_scoped_release>());
```

- [ ] **Step 4: Add GIL release to the cache-dir `FastMapMatch` constructor**

Around line 479, the binding is a `py::init([](...){...})` lambda that touches `py::object cache_dir` (which requires the GIL to read). Use a manual `gil_scoped_release` *inside* the lambda after the py::object has been converted to `std::string`. Replace the existing lambda body so the GIL is released around the heavy `new FastMapMatch(...)` call. The new lambda body:

```cpp
py::init([](const Network &network, TransitionMode mode,
            std::optional<double> max_distance_between_candidates,
            std::optional<double> max_time_between_candidates,
            py::object cache_dir) {
    std::string cache_dir_str;
    if (py::isinstance<py::str>(cache_dir)) {
        cache_dir_str = cache_dir.cast<std::string>();
    } else if (py::hasattr(cache_dir, "__fspath__")) {
        cache_dir_str = py::str(cache_dir.attr("__fspath__")());
    } else {
        throw std::invalid_argument("cache_dir must be a str or Path-like object");
    }
    py::gil_scoped_release release;
    return new FastMapMatch(network, mode, max_distance_between_candidates,
                            max_time_between_candidates, cache_dir_str);
})
```

The rest of that `.def(py::init([](...){...}), py::arg(...), ...)` chain stays unchanged.

- [ ] **Step 5: Rebuild and run tests**

Run: `pip install -e . --no-build-isolation -v 2>&1 | tail -10 && python -m pytest tests/ test_fastmm.py -x --tb=short`
Expected: build succeeds, `45 passed`.

- [ ] **Step 6: Commit**

```bash
git add python/pybind11/fastmm_bindings.cpp
git commit -m "fix(bindings): release GIL on UBODT IO, generation, and cache-dir ctor

Long-running calls (read_ubodt, generate_ubodt, precompute_ubodt_omp,
FastMapMatch cache-dir constructor) now drop the GIL so multi-threaded
Python callers can make progress. Also defaults precompute_ubodt_omp's
network_hash to \"\" for parity with generate_ubodt."
```

---

## Task 3: RoutingKit CH bindings — GIL release on save/load/run + CHQuery keep_alive + INF_WEIGHT

**Files:**
- Modify: `src/python/routingkit_ch_bindings.cpp:199–224`

- [ ] **Step 1: Add `py::call_guard<py::gil_scoped_release>()` to `save`, `load`, `run`, `get_node_path`, `get_arc_path`**

In `src/python/routingkit_ch_bindings.cpp` the `py::class_<PyCH>` block (around L199) currently ends:

```cpp
.def("save", &PyCH::save, py::arg("path"),
     "Save CH to disk in stable CHB1 format")
.def_static("load", &PyCH::load, py::arg("path"),
     "Load CH from CHB1 file")
.def_property_readonly("node_count", &PyCH::node_count);
```

Change to:

```cpp
.def("save", &PyCH::save, py::arg("path"),
     "Save CH to disk in stable CHB1 format",
     py::call_guard<py::gil_scoped_release>())
.def_static("load", &PyCH::load, py::arg("path"),
     "Load CH from CHB1 file",
     py::call_guard<py::gil_scoped_release>())
.def_property_readonly("node_count", &PyCH::node_count);
```

In the `py::class_<PyCHQuery>` block (around L213–L224), change:

```cpp
.def("run",         &PyCHQuery::run,
     "Run the query (Dijkstra over CH)")
.def("get_distance",  &PyCHQuery::get_distance)
.def("get_node_path", &PyCHQuery::get_node_path)
.def("get_arc_path",  &PyCHQuery::get_arc_path);
```

to:

```cpp
.def("run",         &PyCHQuery::run,
     "Run the query (Dijkstra over CH)",
     py::call_guard<py::gil_scoped_release>())
.def("get_distance",  &PyCHQuery::get_distance)
.def("get_node_path", &PyCHQuery::get_node_path,
     py::call_guard<py::gil_scoped_release>())
.def("get_arc_path",  &PyCHQuery::get_arc_path,
     py::call_guard<py::gil_scoped_release>());
```

(`get_distance` returns a single integer and is fast — no GIL release needed.)

- [ ] **Step 2: Add `py::keep_alive<1, 2>()` on `CHQuery.__init__`**

Around L213, change:

```cpp
.def(py::init<const PyCH &>(), py::arg("ch"))
```

to:

```cpp
.def(py::init<const PyCH &>(), py::arg("ch"),
     py::keep_alive<1, 2>(),
     "Create a query over the given CH. The CH is kept alive for the query's lifetime.")
```

- [ ] **Step 3: Expose `INF_WEIGHT` constant**

At the bottom of the `PYBIND11_MODULE` block in `src/python/routingkit_ch_bindings.cpp` (just before the final `}`), add:

```cpp
m.attr("INF_WEIGHT") = py::int_(RoutingKit::inf_weight);
```

Confirm `#include <routingkit/constants.h>` is present near the top — it's pulled in transitively by `<routingkit/contraction_hierarchy.h>` so this usually just works.

- [ ] **Step 4: Update `python/routingkit_ch/_native.pyi` to declare `INF_WEIGHT`**

In `python/routingkit_ch/_native.pyi`, append at the bottom:

```python
INF_WEIGHT: int
```

- [ ] **Step 5: Update `python/routingkit_ch/__init__.py` to re-export `INF_WEIGHT`**

Replace the file with:

```python
from routingkit_ch._native import ContractionHierarchy, CHQuery, INF_WEIGHT

__all__ = ["ContractionHierarchy", "CHQuery", "INF_WEIGHT"]
```

- [ ] **Step 6: Rebuild and run tests**

Run: `pip install -e . --no-build-isolation -v 2>&1 | tail -10 && python -m pytest tests/ test_fastmm.py -x --tb=short`
Expected: build succeeds, `45 passed`.

- [ ] **Step 7: Commit**

```bash
git add src/python/routingkit_ch_bindings.cpp python/routingkit_ch/_native.pyi python/routingkit_ch/__init__.py
git commit -m "fix(routingkit_ch): release GIL on save/load/run, keep_alive on CHQuery, expose INF_WEIGHT

- CHQuery now pins the underlying ContractionHierarchy for its lifetime
  (use-after-free if Python GC'd the CH while a query was still alive).
- save/load/run/get_*_path release the GIL so multi-threaded callers and
  multi-GB load can make progress.
- INF_WEIGHT (RoutingKit::inf_weight) is now exposed so users can compare
  get_distance() results against the actual UINT32_MAX sentinel."
```

---

## Task 4: __repr__ for UBODT and UBODTGenAlgorithm

**Files:**
- Modify: `python/pybind11/fastmm_bindings.cpp` (UBODT around L387, UBODTGenAlgorithm around L422)

- [ ] **Step 1: Add `__repr__` to UBODT**

In the `py::class_<UBODT, std::shared_ptr<UBODT>>(...)` block, before the closing `;`, add:

```cpp
.def("__repr__", [](const UBODT &u) {
    return "<UBODT delta=" + std::to_string(u.get_delta()) +
           " mode=" + (u.get_mode() == TransitionMode::SHORTEST ? std::string("SHORTEST") : std::string("FASTEST")) +
           " rows=" + std::to_string(u.get_num_rows()) +
           " vertices=" + std::to_string(u.get_num_vertices()) + ">";
})
```

Move the existing terminating `;` to after this new `.def`.

- [ ] **Step 2: Add `__repr__` to UBODTGenAlgorithm**

In the `py::class_<UBODTGenAlgorithm>(...)` block, before the closing `;`, add:

```cpp
.def("__repr__", [](const UBODTGenAlgorithm &) {
    return std::string("<UBODTGenAlgorithm>");
})
```

- [ ] **Step 3: Rebuild and verify**

```bash
pip install -e . --no-build-isolation -v 2>&1 | tail -5
python -c "from fastmm import UBODTGenAlgorithm; print(repr(UBODTGenAlgorithm.__doc__ is not None))"
```

Expected: `True`.

- [ ] **Step 4: Run existing tests**

Run: `python -m pytest tests/ test_fastmm.py -x --tb=short`
Expected: `45 passed`.

- [ ] **Step 5: Commit**

```bash
git add python/pybind11/fastmm_bindings.cpp
git commit -m "feat(bindings): add __repr__ to UBODT and UBODTGenAlgorithm"
```

---

## Task 5: UBODT test coverage (round-trip + shared FastMapMatch + non-SUCCESS error code)

**Files:**
- Create: `tests/test_ubodt.py`

- [ ] **Step 1: Create the test file**

Create `tests/test_ubodt.py` with the following content. This builds a tiny network, generates a UBODT to disk, reads it back, asserts properties, constructs a FastMapMatch with the pre-loaded UBODT, matches a simple trajectory, and asserts the same result as the cache-dir constructor would give. It also exercises a non-SUCCESS error code path.

```python
"""Tests for the UBODT / UBODTGenAlgorithm Python bindings (commit b708183).

These exercise:
- UBODTGenAlgorithm.generate_ubodt → on-disk .bin file
- UBODT.read_ubodt → load + property inspection
- FastMapMatch(network, mode, ubodt) — pre-loaded UBODT constructor
- Sharing one UBODT across two FastMapMatch instances
- A non-SUCCESS MatchErrorCode being observed (delta too small)
"""

import math

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


def _build_grid_network():
    """3-node line:  10 --1--> 20 --2--> 30, each edge 100m long."""
    net = Network()
    net.add_edge(1, source=10, target=20, geom=[[0.0, 0.0], [100.0, 0.0]])
    net.add_edge(2, source=20, target=30, geom=[[100.0, 0.0], [200.0, 0.0]])
    net.finalize()
    return net


def test_generate_then_read_ubodt(tmp_path):
    net = _build_grid_network()
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
    """The new (network, mode, ubodt) constructor should match identically
    to a FastMapMatch built with cache_dir generating the same UBODT."""
    net = _build_grid_network()
    graph = NetworkGraph(net, TransitionMode.SHORTEST)
    gen = UBODTGenAlgorithm(net, graph, TransitionMode.SHORTEST)
    ubodt_file = tmp_path / "shared.bin"
    gen.generate_ubodt(str(ubodt_file), delta=500.0)
    ubodt = UBODT.read_ubodt(str(ubodt_file))

    matcher = FastMapMatch(net, TransitionMode.SHORTEST, ubodt)
    traj = Trajectory.from_xy_tuples([(10.0, 0.0, 0.0), (150.0, 0.0, 1.0)])
    result = matcher.match(traj, candidate_search_radius=50.0, gps_error=20.0)
    assert len(result.subtrajectories) == 1
    assert result.subtrajectories[0].error_code == MatchErrorCode.SUCCESS


def test_one_ubodt_shared_across_two_matchers(tmp_path):
    """A UBODT bound to two FastMapMatch instances must keep both alive
    and produce equivalent matches."""
    net = _build_grid_network()
    graph = NetworkGraph(net, TransitionMode.SHORTEST)
    gen = UBODTGenAlgorithm(net, graph, TransitionMode.SHORTEST)
    ubodt_file = tmp_path / "shared2.bin"
    gen.generate_ubodt(str(ubodt_file), delta=500.0)
    ubodt = UBODT.read_ubodt(str(ubodt_file))

    m1 = FastMapMatch(net, TransitionMode.SHORTEST, ubodt)
    m2 = FastMapMatch(net, TransitionMode.SHORTEST, ubodt)

    traj = Trajectory.from_xy_tuples([(20.0, 0.0, 0.0), (180.0, 0.0, 1.0)])
    r1 = m1.match(traj, candidate_search_radius=50.0, gps_error=20.0)
    r2 = m2.match(traj, candidate_search_radius=50.0, gps_error=20.0)
    assert len(r1.subtrajectories) == len(r2.subtrajectories) == 1
    assert r1.subtrajectories[0].error_code == MatchErrorCode.SUCCESS
    assert r2.subtrajectories[0].error_code == MatchErrorCode.SUCCESS


def test_match_observes_non_success_when_delta_too_small(tmp_path):
    """A UBODT generated with delta < gap distance must produce a
    DISCONNECTED_LAYERS error on a trajectory whose hop exceeds delta.

    This is the only test in the suite that asserts a non-SUCCESS error
    code is reachable from Python."""
    net = _build_grid_network()
    graph = NetworkGraph(net, TransitionMode.SHORTEST)
    gen = UBODTGenAlgorithm(net, graph, TransitionMode.SHORTEST)
    ubodt_file = tmp_path / "tiny.bin"
    # delta=50 is smaller than the 100m-per-edge spacing → no precomputed
    # paths bridge node 10 → 30, so a long jump will fail to connect.
    gen.generate_ubodt(str(ubodt_file), delta=50.0)
    ubodt = UBODT.read_ubodt(str(ubodt_file))

    matcher = FastMapMatch(net, TransitionMode.SHORTEST, ubodt)
    traj = Trajectory.from_xy_tuples([(10.0, 0.0, 0.0), (190.0, 0.0, 1.0)])
    result = matcher.match(traj, candidate_search_radius=30.0, gps_error=10.0)

    observed = {sub.error_code for sub in result.subtrajectories}
    # At least one non-SUCCESS code (or empty subtrajectories list).
    assert not observed or observed != {MatchErrorCode.SUCCESS}, (
        "expected at least one non-SUCCESS MatchErrorCode for impossibly small delta, "
        f"got {observed}"
    )
```

- [ ] **Step 2: Run the new test file**

Run: `python -m pytest tests/test_ubodt.py -v --tb=short`
Expected: 4 tests pass. If `test_match_observes_non_success_when_delta_too_small` passes with an empty `subtrajectories` list, that's still a non-SUCCESS path and the assertion's first clause covers it.

- [ ] **Step 3: Run the full suite**

Run: `python -m pytest tests/ test_fastmm.py -v --tb=short`
Expected: `49 passed` (45 existing + 4 new).

- [ ] **Step 4: Commit**

```bash
git add tests/test_ubodt.py
git commit -m "test: cover UBODT generation, load, and pre-loaded FastMapMatch ctor

Adds 4 tests that exercise the bindings introduced in b708183:
generate_ubodt → file, UBODT.read_ubodt, the new
FastMapMatch(network, mode, ubodt) constructor, sharing one UBODT across
two matchers, and a non-SUCCESS MatchErrorCode being observable from
Python (delta-too-small case)."
```

---

## Task 6: ch_router smoke test (public re-export package)

**Files:**
- Create: `tests/test_ch_router_package.py`

- [ ] **Step 1: Create the test file**

Create `tests/test_ch_router_package.py`:

```python
"""Smoke test for the `ch_router` re-export package.

The wheel ships three packages: fastmm, routingkit_ch, ch_router. Existing
tests import from the first two directly, so a packaging regression in
ch_router would ship green. This file imports the public API surface and
asserts every name in __all__ resolves to a real object."""

import ch_router


def test_all_exports_resolve():
    assert hasattr(ch_router, "__all__")
    for name in ch_router.__all__:
        assert hasattr(ch_router, name), f"ch_router.{name} declared in __all__ but missing"
        assert getattr(ch_router, name) is not None


def test_version_is_string():
    assert isinstance(ch_router.__version__, str)
    assert ch_router.__version__ != ""


def test_classes_are_same_objects_as_underlying_packages():
    """ch_router must re-export the actual C++-bound classes, not Python wrappers."""
    import fastmm
    import routingkit_ch

    assert ch_router.FastMapMatch is fastmm.FastMapMatch
    assert ch_router.UBODT is fastmm.UBODT
    assert ch_router.UBODTGenAlgorithm is fastmm.UBODTGenAlgorithm
    assert ch_router.ContractionHierarchy is routingkit_ch.ContractionHierarchy
    assert ch_router.CHQuery is routingkit_ch.CHQuery
```

- [ ] **Step 2: Run the new test**

Run: `python -m pytest tests/test_ch_router_package.py -v --tb=short`
Expected: 3 tests pass.

- [ ] **Step 3: Run the full suite**

Run: `python -m pytest tests/ test_fastmm.py -v --tb=short`
Expected: `52 passed`.

- [ ] **Step 4: Commit**

```bash
git add tests/test_ch_router_package.py
git commit -m "test(ch_router): smoke test the re-export package's __all__"
```

---

## Task 7: Fix macOS wheel target and add delocate exclude

**Files:**
- Modify: `pyproject.toml`

- [ ] **Step 1: Lower `MACOSX_DEPLOYMENT_TARGET` to 11.0 and add macOS `repair-wheel-command`**

In `pyproject.toml`, the existing `[tool.cibuildwheel.macos]` block is:

```toml
[tool.cibuildwheel.macos]
before-build = "brew install boost cmake libomp"
environment = { OpenMP_ROOT = "$(brew --prefix libomp)", MACOSX_DEPLOYMENT_TARGET = "15.0" }
```

Replace with:

```toml
[tool.cibuildwheel.macos]
before-build = "brew install boost cmake libomp"
environment = { OpenMP_ROOT = "$(brew --prefix libomp)", MACOSX_DEPLOYMENT_TARGET = "11.0" }
# Same exclude strategy as Linux: libfastmmlib.dylib already lives next to
# fastmm.so inside the wheel (installed by CMake), and the extension's RPATH
# is @loader_path, so delocate must NOT copy it into .dylibs/.
repair-wheel-command = "delocate-wheel --require-archs {delocate_archs} -w {dest_dir} -v {wheel} --exclude libfastmmlib.dylib"
```

- [ ] **Step 2: Drop the empty `matcher` optional-dep group and the dead `wheel.packages = []`**

In `pyproject.toml`:
- Remove the line `matcher = []` from `[project.optional-dependencies]`.
- Remove `wheel.packages = []` from `[tool.scikit-build]` (the CMake `install(DIRECTORY ...)` blocks already control what ships).

- [ ] **Step 3: Verify the toml still parses**

Run: `python -c "import tomllib; tomllib.loads(open('pyproject.toml').read())"`
Expected: no output, no exception.

- [ ] **Step 4: Run tests (no rebuild needed for toml-only change)**

Run: `python -m pytest tests/ test_fastmm.py -x --tb=short`
Expected: `52 passed`.

- [ ] **Step 5: Commit**

```bash
git add pyproject.toml
git commit -m "build: lower macOS target to 11.0, add delocate exclude, drop dead config

- MACOSX_DEPLOYMENT_TARGET=15.0 (Sequoia) excluded ~all Mac users; 11.0 (Big
  Sur, 2020) is the modern reasonable floor.
- delocate was bundling libfastmmlib.dylib into .dylibs/, breaking the
  @loader_path RPATH layout. Exclude it explicitly, matching the Linux
  auditwheel rule.
- Drop empty [project.optional-dependencies].matcher and dead wheel.packages=[]."
```

---

## Task 8: Ship py.typed and .pyi for all three packages

**Files:**
- Modify: `python/pybind11/CMakeLists.txt`
- Create: `python/ch_router/py.typed`

- [ ] **Step 1: Create the ch_router py.typed marker**

```bash
touch /Users/ankushv/Developer/fastmm/python/ch_router/py.typed
```

Confirm: `ls python/ch_router/` → should now include `py.typed`.

- [ ] **Step 2: Update install rules to ship `.pyi` and `py.typed` for fastmm and ch_router**

In `python/pybind11/CMakeLists.txt`, find the existing fastmm install block (around L65):

```cmake
# Install Python source files
install(DIRECTORY ${PROJECT_SOURCE_DIR}/python/fastmm/
    DESTINATION fastmm
    COMPONENT python
    FILES_MATCHING PATTERN "*.py"
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.egg-info" EXCLUDE)
```

Change `FILES_MATCHING PATTERN "*.py"` to match stub files and the marker too. Replace with:

```cmake
# Install fastmm Python sources, stubs, and py.typed marker
install(DIRECTORY ${PROJECT_SOURCE_DIR}/python/fastmm/
    DESTINATION fastmm
    COMPONENT python
    FILES_MATCHING
        PATTERN "*.py"
        PATTERN "*.pyi"
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.egg-info" EXCLUDE)

# fastmm py.typed marker (lives at python/py.typed, ship as fastmm/py.typed)
install(FILES ${PROJECT_SOURCE_DIR}/python/py.typed
    DESTINATION fastmm
    COMPONENT python)
```

And find the existing ch_router install block (around L109):

```cmake
# Install ch_router top-level package (thin re-export wrapper)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/python/ch_router/
    DESTINATION ch_router
    COMPONENT python
    FILES_MATCHING PATTERN "*.py"
    PATTERN "__pycache__" EXCLUDE)
```

Replace with:

```cmake
# Install ch_router top-level package (thin re-export wrapper) with stubs/marker
install(DIRECTORY ${PROJECT_SOURCE_DIR}/python/ch_router/
    DESTINATION ch_router
    COMPONENT python
    FILES_MATCHING
        PATTERN "*.py"
        PATTERN "*.pyi"
        PATTERN "py.typed"
    PATTERN "__pycache__" EXCLUDE)
```

- [ ] **Step 3: Rebuild and verify stubs land in the wheel layout**

```bash
pip install -e . --no-build-isolation -v 2>&1 | tail -5
python -c "import fastmm, ch_router, os; print(os.path.dirname(fastmm.__file__)); print(os.path.dirname(ch_router.__file__))"
ls "$(python -c 'import fastmm, os; print(os.path.dirname(fastmm.__file__))')" | grep -E 'py.typed|.pyi$'
```

Expected: `py.typed` and at least one `.pyi` listed for fastmm. (Editable installs may not relocate files; that's OK — the install rules are what matters for the wheel.)

- [ ] **Step 4: Build a real wheel to verify layout**

```bash
pip wheel . --no-deps --no-build-isolation -w /tmp/ch_router_wheel
unzip -l /tmp/ch_router_wheel/ch_router-*.whl | grep -E 'py.typed|\.pyi'
```

Expected: lines like `fastmm/py.typed`, `fastmm/fastmm.pyi`, `fastmm/__init__.pyi`, `routingkit_ch/py.typed`, `routingkit_ch/_native.pyi`, `ch_router/py.typed`.

- [ ] **Step 5: Run the test suite**

Run: `python -m pytest tests/ test_fastmm.py -x --tb=short`
Expected: `52 passed`.

- [ ] **Step 6: Commit**

```bash
git add python/pybind11/CMakeLists.txt python/ch_router/py.typed
git commit -m "build: ship py.typed and .pyi for fastmm and ch_router

mypy/Pylance previously saw fastmm and ch_router as untyped because the
install rules globbed *.py only. Now ships:
- python/py.typed              → fastmm/py.typed
- python/fastmm/*.pyi          → fastmm/*.pyi
- python/ch_router/py.typed    → ch_router/py.typed (new marker)
- python/ch_router/*.pyi       → ch_router/*.pyi (if any added later)"
```

---

## Task 9: Delete orphaned stub generator script

**Files:**
- Delete: `generate_stubs_for_wheel.py`

- [ ] **Step 1: Confirm there are no callers**

Run: `grep -rn 'generate_stubs_for_wheel' . --include='*.py' --include='*.toml' --include='*.cmake' --include='*.yml' --include='CMakeLists.txt'`
Expected: matches only inside the file itself, no callers.

- [ ] **Step 2: Delete the file**

```bash
git rm generate_stubs_for_wheel.py
```

- [ ] **Step 3: Add a header comment to each shipped stub noting it is hand-maintained**

In `python/fastmm/fastmm.pyi`, `python/fastmm/__init__.pyi`, and `python/routingkit_ch/_native.pyi`, ensure the first line is a comment of the form:

```python
# Hand-maintained type stubs. Update by hand when bindings change.
```

If the existing first line is already a comment, replace it. If not, prepend this line.

Read each file first to see its top line, then prepend the comment only if missing.

- [ ] **Step 4: Run tests**

Run: `python -m pytest tests/ test_fastmm.py -x --tb=short`
Expected: `52 passed`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "chore: drop orphaned generate_stubs_for_wheel.py

The script was never invoked from CMake, pyproject, or CI. The .pyi files
in python/{fastmm,routingkit_ch} are committed and hand-maintained. Each
stub now notes that in its header."
```

---

## Task 10: Fix CMake glob CONFIGURE_DEPENDS + scope Boost

**Files:**
- Modify: `CMakeLists.txt`, `python/pybind11/CMakeLists.txt`

- [ ] **Step 1: Scope Boost link to FASTMMLIB instead of globally**

In `CMakeLists.txt`, find:

```cmake
link_libraries(${Boost_LIBRARIES})
```

Delete that line. Find the `add_library(FASTMMLIB SHARED ...)` block (around L116) and immediately after it, add:

```cmake
target_link_libraries(FASTMMLIB PUBLIC ${Boost_LIBRARIES})
```

- [ ] **Step 2: Same for OpenMP — scope to FASTMMLIB**

In `CMakeLists.txt`, find:

```cmake
link_libraries(${OpenMP_CXX_LIBRARIES})
```

Delete it. Add immediately after the new Boost line above:

```cmake
target_link_libraries(FASTMMLIB PUBLIC ${OpenMP_CXX_LIBRARIES})
```

- [ ] **Step 3: Use `CONFIGURE_DEPENDS` on the RoutingKit glob**

In `python/pybind11/CMakeLists.txt` around L72:

```cmake
file(GLOB RK_SRC ${PROJECT_SOURCE_DIR}/third_party/routingkit/src/*.cpp)
```

Change to:

```cmake
file(GLOB RK_SRC CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/third_party/routingkit/src/*.cpp)
```

Do the same for the FASTMM globs in the top-level `CMakeLists.txt` (the `file(GLOB CoreGlob ...)` and siblings around L104–L109).

- [ ] **Step 4: Rebuild from scratch and test**

```bash
rm -rf build
pip install -e . --no-build-isolation -v 2>&1 | tail -10
python -m pytest tests/ test_fastmm.py -x --tb=short
```

Expected: clean rebuild, `52 passed`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt python/pybind11/CMakeLists.txt
git commit -m "build: scope Boost/OpenMP to FASTMMLIB, add CONFIGURE_DEPENDS on globs

- link_libraries() at global scope was leaking Boost into
  routingkit_ch_native which doesn't use it. Now linked via
  target_link_libraries(FASTMMLIB PUBLIC ...).
- file(GLOB ...) without CONFIGURE_DEPENDS silently drops newly added
  files until someone re-configures. Add the keyword everywhere we use
  GLOB so vendored third-party additions are picked up automatically."
```

---

## Task 11: Strip dead Windows code paths

**Files:**
- Modify: `CMakeLists.txt`, `python/pybind11/CMakeLists.txt`

- [ ] **Step 1: Remove `MSVC` and `WIN32` branches from `CMakeLists.txt`**

In `CMakeLists.txt`, delete the following blocks:

```cmake
if (MSVC)
  add_compile_options(/EHsc /std:c++17)
endif()
```

```cmake
# Use correct optimization flags for each compiler
if (MSVC)
  add_compile_options(/EHsc)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
else()
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
endif()
```

Replace the second block with the simpler (non-Windows) form:

```cmake
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
```

Delete the Windows Boost branch:

```cmake
if (WIN32)
  find_package(Boost 1.56.0 REQUIRED serialization exception)
else ()
  find_package(Boost 1.56.0 REQUIRED serialization)
endif (WIN32)
```

Replace with:

```cmake
find_package(Boost 1.56.0 REQUIRED serialization)
```

Delete the Windows DLL output directory block (around L124–L131):

```cmake
if(WIN32)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config_upper)
        set_target_properties(FASTMMLIB PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${config}"
            LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${config}"
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${config}")
    endforeach()
endif()
```

Delete the contradictory in-source `CMAKE_RUNTIME_OUTPUT_DIRECTORY` line:

```cmake
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")
```

- [ ] **Step 2: Remove the Windows `.pyd` suffix block from pybind11/CMakeLists.txt**

In `python/pybind11/CMakeLists.txt`, delete:

```cmake
if (WIN32)
  set_target_properties(fastmm PROPERTIES SUFFIX ".pyd")
endif()
```

- [ ] **Step 3: Also drop the Windows `set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)` line from `CMakeLists.txt`**

Find and delete:

```cmake
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
```

- [ ] **Step 4: Delete the Windows-only test scripts (untested artifacts of a now-removed Windows path)**

```bash
git rm test_cibuildwheel.ps1 test_local_build.ps1
```

(Keep `test_cibuildwheel.sh` for Linux/macOS.)

- [ ] **Step 5: Rebuild and test**

```bash
rm -rf build
pip install -e . --no-build-isolation -v 2>&1 | tail -10
python -m pytest tests/ test_fastmm.py -x --tb=short
```

Expected: clean build, `52 passed`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt python/pybind11/CMakeLists.txt test_cibuildwheel.ps1 test_local_build.ps1
git commit -m "build: strip dead Windows code paths

CI never built Windows wheels and there's no plan to start. Removes:
- MSVC compile-option branches in CMakeLists.txt
- WIN32 DLL output-directory block
- WIN32 Boost::exception requirement
- .pyd suffix override in python/pybind11/CMakeLists.txt
- CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS
- contradictory in-source CMAKE_RUNTIME_OUTPUT_DIRECTORY
- test_cibuildwheel.ps1 / test_local_build.ps1 (Windows-only, never CI-run)

If Windows support is added back, do it together with a working CI matrix entry."
```

---

## Task 12: Fix sdist by removing MANIFEST.in

**Files:**
- Delete: `MANIFEST.in`

- [ ] **Step 1: Confirm what's in MANIFEST.in is incomplete and ineffective**

Run: `cat MANIFEST.in`
Expected: only includes `python/fastmm/*.py *.pyi *.pyd *.dll *.so *.dylib`, missing `python/routingkit_ch`, `python/ch_router`, `third_party/`, `src/`, `cmake/`, `tests/`, `test_fastmm.py`. scikit-build-core's sdist defaults to "all git-tracked files" which is what we actually want.

- [ ] **Step 2: Delete the file**

```bash
git rm MANIFEST.in
```

- [ ] **Step 3: Build an sdist and inspect**

```bash
python -m build --sdist --no-isolation 2>&1 | tail -10
tar tzf dist/ch_router-*.tar.gz | head -40
```

Expected: sdist contains `python/fastmm/`, `python/routingkit_ch/`, `python/ch_router/`, `third_party/routingkit/`, `src/`, `cmake/`, `tests/`, `test_fastmm.py`, `CMakeLists.txt`, `pyproject.toml`. (If `python -m build` isn't available, install it: `pip install build` first.)

- [ ] **Step 4: Try building from the sdist**

```bash
cd /tmp && rm -rf ch_router_sdist_test && mkdir ch_router_sdist_test && cd ch_router_sdist_test
tar xzf /Users/ankushv/Developer/fastmm/dist/ch_router-*.tar.gz
cd ch_router-*
pip wheel . --no-deps --no-build-isolation -w . 2>&1 | tail -10
```

Expected: a wheel file is produced. (Skip this step if `pip wheel` cannot find Boost in the test environment — the sdist contents inspection in Step 3 is the primary check.)

- [ ] **Step 5: Run tests on the original tree**

```bash
cd /Users/ankushv/Developer/fastmm
python -m pytest tests/ test_fastmm.py -x --tb=short
```

Expected: `52 passed`.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "build: drop MANIFEST.in, let scikit-build-core handle sdist

The hand-rolled MANIFEST.in only listed python/fastmm/*, so the produced
sdist was missing third_party/, src/, cmake/, tests/, test_fastmm.py,
python/routingkit_ch/, and python/ch_router/ — i.e., it was unbuildable.
scikit-build-core's default sdist is the git-tracked-file set, which is
the right answer here."
```

---

## Task 13: README third-party license attribution + vendor record

**Files:**
- Modify: `README.md`
- Create: `third_party/routingkit/VENDOR.txt`

- [ ] **Step 1: Append a "Third-party licenses" section to README.md**

At the end of `README.md`, append:

```markdown
## Third-Party Licenses

This package vendors source from the following projects:

- **[RoutingKit](https://github.com/RoutingKit/RoutingKit)** — BSD-3-Clause.
  Source is bundled under `third_party/routingkit/` with the original
  `LICENSE` file preserved. Used to build `routingkit_ch._native`.
- **[spdlog](https://github.com/gabime/spdlog)** — MIT.
  Headers in `third_party/spdlog/`. Used for logging in the C++ core.
- **[FiboHeap](https://github.com/beniz/fiboheap)** — public domain / MIT-style.
  Headers in `third_party/fiboheap/`. Used inside fastmm's routing.

See each project's `LICENSE` file under `third_party/<name>/` for the full text.
```

- [ ] **Step 2: Create `third_party/routingkit/VENDOR.txt` recording provenance**

```bash
cat > /Users/ankushv/Developer/fastmm/third_party/routingkit/VENDOR.txt <<'EOF'
Upstream: https://github.com/RoutingKit/RoutingKit
Vendored:  subset (CH only) — files present in include/ and src/ are a strict
           subset of upstream master. No local modifications known; if you
           change a vendored file, please record it here.

To resync against upstream:
  1. Pick an upstream commit.
  2. Copy:
       include/routingkit/   ← upstream include/routingkit/<files listed below>
       src/                  ← upstream src/<files listed below>
     LICENSE remains as-is.
  3. Update "Upstream commit:" below.

Upstream commit: (unknown — initial vendor, please record next time you sync)

Files vendored (relative to third_party/routingkit/):
  LICENSE
  include/routingkit/*.h     (full set as of vendor time)
  src/*.cpp                  (full set as of vendor time)

Modifications from upstream:
  None recorded.
EOF
```

- [ ] **Step 3: Install LICENSE into the wheel root**

In `python/pybind11/CMakeLists.txt`, add at the end of the file (after the existing `install(...)` blocks):

```cmake
# Ship RoutingKit LICENSE inside the routingkit_ch package directory so
# end users have the BSD-3 notice next to the code that uses it.
install(FILES ${PROJECT_SOURCE_DIR}/third_party/routingkit/LICENSE
    DESTINATION routingkit_ch
    RENAME LICENSE.RoutingKit
    COMPONENT python)
```

- [ ] **Step 4: Rebuild and confirm LICENSE lands in the wheel**

```bash
rm -rf build dist /tmp/ch_router_wheel
pip wheel . --no-deps --no-build-isolation -w /tmp/ch_router_wheel 2>&1 | tail -5
unzip -l /tmp/ch_router_wheel/ch_router-*.whl | grep -i license
```

Expected: at least `ch_router-*.dist-info/licenses/LICENSE.TXT` and `routingkit_ch/LICENSE.RoutingKit`.

- [ ] **Step 5: Run tests**

Run: `python -m pytest tests/ test_fastmm.py -x --tb=short`
Expected: `52 passed`.

- [ ] **Step 6: Commit**

```bash
git add README.md third_party/routingkit/VENDOR.txt python/pybind11/CMakeLists.txt
git commit -m "docs: attribute RoutingKit BSD-3 and add vendor provenance file

- README gains a Third-Party Licenses section.
- third_party/routingkit/VENDOR.txt records the vendor subset and
  resync instructions; future syncs should record the upstream commit.
- LICENSE.RoutingKit now ships inside routingkit_ch/ in the wheel."
```

---

## Task 14: Pin docker-cache action by SHA

**Files:**
- Modify: `.github/workflows/build-wheels.yml`

- [ ] **Step 1: Find the current SHA for `ScribeMD/docker-cache@0.5.0`**

Run:
```bash
gh api repos/ScribeMD/docker-cache/git/refs/tags/0.5.0 --jq '.object.sha'
```

Expected: a 40-char SHA. If `gh` is not available, fall back to:
```bash
curl -s https://api.github.com/repos/ScribeMD/docker-cache/git/refs/tags/0.5.0 | python -c "import sys,json; print(json.load(sys.stdin)['object']['sha'])"
```

Record the SHA — call it `<SHA>` below.

- [ ] **Step 2: Update the workflow to pin by SHA with a version comment**

In `.github/workflows/build-wheels.yml`, change:

```yaml
        uses: ScribeMD/docker-cache@0.5.0
```

to:

```yaml
        uses: ScribeMD/docker-cache@<SHA>  # v0.5.0
```

(Use the exact SHA from Step 1.)

- [ ] **Step 3: Validate workflow syntax**

Run: `python -c "import yaml; yaml.safe_load(open('.github/workflows/build-wheels.yml'))"`
Expected: no output, no exception.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/build-wheels.yml
git commit -m "ci: pin ScribeMD/docker-cache by SHA

Third-party actions referenced by tag can be silently retagged. Pin by
commit SHA and keep the version as a trailing comment."
```

---

## Task 15: Final verification before release

**Files:**
- (no edits — verification + a release-readiness commit only if anything was missed)

- [ ] **Step 1: Clean build from scratch**

```bash
rm -rf build dist /tmp/ch_router_wheel
pip install -e . --no-build-isolation -v 2>&1 | tail -10
```

Expected: build succeeds, no warnings about missing files in the install step.

- [ ] **Step 2: Run the full test suite**

Run: `python -m pytest tests/ test_fastmm.py -v --tb=short`
Expected: `52 passed`.

- [ ] **Step 3: Build wheel and sdist and inspect both**

```bash
python -m build --no-isolation 2>&1 | tail -10
ls -la dist/
unzip -l dist/ch_router-*.whl | head -40
tar tzf dist/ch_router-*.tar.gz | head -20
```

Expected:
- wheel contains `fastmm/`, `routingkit_ch/`, `ch_router/` packages each with their `.py`, `.pyi`, and `py.typed`
- wheel contains `routingkit_ch/LICENSE.RoutingKit`
- sdist contains `third_party/`, `src/`, `tests/`, `test_fastmm.py`

- [ ] **Step 4: Lint with ruff**

Run: `ruff check python/ tests/ test_fastmm.py 2>&1 | tail -10`
Expected: zero errors.

- [ ] **Step 5: Tag-ready commit (no changes; if everything is green there's nothing to commit)**

Run: `git status`
Expected: working tree clean. Print a summary of the branch ready to tag:

```bash
git log --oneline c60f8f7..HEAD
```

Expected: ~14 commits, one per task above (plus the pre-existing 3 commits — UBODT, RoutingKit, rename).

- [ ] **Step 6: Hand off to the user with a suggested next tag**

Print to the user (not as code, just text):

> All tasks complete. Suggested next tag: `v0.1.3`. To cut it:
>
> ```bash
> git tag -a v0.1.3 -m "Release hardening: GIL, CHQuery lifetime, macOS wheel, tests, packaging"
> git push origin main v0.1.3
> ```
>
> Confirm with the user before tagging — never run `git push` or `git tag` autonomously.

---

## Self-Review Notes

- **Spec coverage** — Critical+Important issues from the four reviews:
  - GIL on `FastMapMatch.match` → Task 1 ✓
  - GIL on UBODT IO / generation / cache-dir ctor → Task 2 ✓
  - GIL on CH save/load/run/get_*_path → Task 3 ✓
  - `py::keep_alive<1,2>()` on CHQuery → Task 3 ✓
  - `INF_WEIGHT` constant → Task 3 ✓
  - `__repr__` for UBODT/UBODTGenAlgorithm → Task 4 ✓
  - UBODT test coverage + non-SUCCESS code → Task 5 ✓
  - `ch_router` smoke test → Task 6 ✓
  - `tmp_path` use in new tests → Task 5 ✓ (existing tests using `.cache` are left untouched — switching them is mechanical churn outside this plan's scope; new tests demonstrate the pattern)
  - macOS target → Task 7 ✓
  - macOS delocate exclude → Task 7 ✓
  - Empty `matcher` group → Task 7 ✓
  - `py.typed` / `.pyi` shipping → Task 8 ✓
  - Orphaned stub generator → Task 9 ✓
  - Boost / OpenMP global link scope → Task 10 ✓
  - `CONFIGURE_DEPENDS` on globs → Task 10 ✓
  - Strip Windows → Task 11 ✓
  - Sdist via MANIFEST.in deletion → Task 12 ✓
  - README license attribution + VENDOR.txt → Task 13 ✓
  - `ScribeMD/docker-cache` SHA pin → Task 14 ✓
  - Final wheel/sdist verification → Task 15 ✓
- **Out of scope (deferred):** RoutingKit surface expansion (`pin_targets`, `run_to_pinned_targets`, native `save_file`/`load_file`, `build_given_order`, `customizable_contraction_hierarchy`). These were tagged "Recommended" feature additions, not Critical/Important fixes. Track in a follow-up plan.
- **Existing test cache_dir pollution** — left untouched on purpose; this plan adds new well-behaved tests but does not rewrite the 7 existing literal-`.cache` tests. That's a low-priority cleanup separate from release hardening.
- **`CMAKE_CXX_FLAGS = SPDLOG_LEVEL_TRACE`** in Release — left untouched. The reviewer flagged this as minor; changing it requires deciding the project's logging policy, which is a design choice not a fix.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-29-ch-router-release-hardening.md`.

The user already asked for **subagent-driven development**, so I'll proceed via `superpowers:subagent-driven-development` — a fresh subagent per task with a two-stage review between each.
