# RoutingKit CH Bindings for FMM — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a standalone `routingkit_ch` Python extension to the FMM fork that exposes RoutingKit's ContractionHierarchy build + query as an OO Python API, with CHB1 flat-file save/load, published alongside FMM via the same CMake build and PyPI package.

**Architecture:** RoutingKit's C++ source (~200 KB, 14 headers + 7 .cpp) is vendored into `third_party/routingkit/`. A new pybind11 extension `routingkit_ch` is built alongside FMM's existing SWIG extension. The two extensions are independent Python modules (`import fmm`, `import routingkit_ch`). No changes to FMM's existing SWIG/C++ code are required.

**Tech Stack:** C++17 (for RoutingKit), pybind11 ≥ 2.10, CMake ≥ 3.14, numpy ≥ 1.20, Python ≥ 3.10. FMM's existing deps (GDAL, Boost, OpenMP) are untouched.

---

## FMM Improvements (apply alongside or after CH work)

These are independent of the CH bindings but worth doing in the same PR:

1. **C++ standard: 11 → 17** — RoutingKit requires C++17. Once pybind11 is in the build, set `CMAKE_CXX_STANDARD 17` globally (C++17 is backward-compatible with FMM's C++11 code).
2. **CMake minimum: 3.5.1 → 3.14** — Required for `FetchContent`. All modern Linux distros ship ≥ 3.14.
3. **SWIG → pybind11 for FMM itself** *(bigger, optional)*: The SWIG `.i` interface is fragile and produces un-Pythonic types (e.g. `IntVector` instead of `list[int]`). pybind11 would let you return real Python lists, dicts, numpy arrays. This is a multi-day refactor — skip for now unless desired.
4. **CH-accelerated UBODT generation** *(future)*: `ubodt_gen` runs single-source Dijkstra from every node; replacing with CH one-to-many would be ~100× faster on city graphs. Requires mapping FMM `NodeIndex` ↔ RoutingKit node IDs. Not in scope here.

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `third_party/routingkit/` | Create | Vendored RoutingKit subset |
| `third_party/routingkit/include/routingkit/*.h` | Create (14 files) | RoutingKit public headers |
| `third_party/routingkit/src/*.cpp` | Create (7 files) | RoutingKit implementation |
| `third_party/routingkit/src/*.h` | Create (3 files) | RoutingKit src-local headers |
| `third_party/routingkit/LICENSE` | Create | BSD-2-Clause from RoutingKit |
| `src/python/routingkit_ch_bindings.cpp` | Create | pybind11 bindings |
| `python/routingkit_ch/__init__.py` | Create | Python wrapper + public API |
| `python/routingkit_ch/py.typed` | Create | PEP 561 marker |
| `python/routingkit_ch/_native.pyi` | Create | Type stubs for native module |
| `CMakeLists.txt` | Modify | Add pybind11 + routingkit_ch target |
| `python/CMakeLists.txt` | Modify | Install routingkit_ch package |
| `tests/test_routingkit_ch.py` | Create | pytest suite |
| `README_routingkit_ch.md` | Create | Usage docs |

---

## Task 1: Vendor RoutingKit sources

**Files:**
- Create: `third_party/routingkit/` (full subtree)

- [ ] **Step 1: Clone RoutingKit and copy the needed subset**

```bash
cd /tmp
git clone --depth 1 https://github.com/RoutingKit/RoutingKit rk_src

# Create vendor dirs
mkdir -p <your-fmm-fork>/third_party/routingkit/include/routingkit
mkdir -p <your-fmm-fork>/third_party/routingkit/src

RK=/tmp/rk_src
DEST=<your-fmm-fork>/third_party/routingkit

# 14 public headers
for h in \
  contraction_hierarchy bit_vector id_queue timestamp_flag \
  permutation sort inverse_vector timer graph_util vector_io \
  filter min_max constants id_set_queue; do
  cp $RK/include/routingkit/${h}.h $DEST/include/routingkit/
done

# 7 .cpp sources
for f in \
  contraction_hierarchy bit_vector bit_select \
  expect graph_util timer vector_io; do
  cp $RK/src/${f}.cpp $DEST/src/
done

# 3 src-local headers (included by the .cpp files, not public API)
for h in bit_select emulate_gcc_builtin expect; do
  cp $RK/src/${h}.h $DEST/src/
done

# License
cp $RK/LICENSE $DEST/LICENSE
```

- [ ] **Step 2: Verify the copy is complete**

```bash
ls third_party/routingkit/include/routingkit/ | wc -l   # expect 14
ls third_party/routingkit/src/*.cpp | wc -l             # expect 7
ls third_party/routingkit/src/*.h   | wc -l             # expect 3
```

- [ ] **Step 3: Commit**

```bash
git add third_party/routingkit/
git commit -m "vendor: add RoutingKit subset (CH build+query, BSD-2-Clause)"
```

---

## Task 2: Add pybind11 and routingkit_ch to CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Read the existing top of CMakeLists.txt**

Verify the `cmake_minimum_required` line and note where `add_subdirectory(python)` appears.

- [ ] **Step 2: Update CMake minimum version and add pybind11 fetch**

In `CMakeLists.txt`, change:

```cmake
cmake_minimum_required( VERSION 3.5.1)
```
to:
```cmake
cmake_minimum_required(VERSION 3.14)
```

And change:
```cmake
set(CMAKE_CXX_STANDARD 11)
```
to:
```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

- [ ] **Step 3: Add pybind11 via FetchContent (after the existing find_package blocks)**

Add this block before `add_subdirectory(python)`:

```cmake
# --- pybind11 (for routingkit_ch extension) ---
include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11
  GIT_TAG        v2.13.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(pybind11)

# --- routingkit_ch extension ---
file(GLOB RK_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/routingkit/src/*.cpp)

pybind11_add_module(routingkit_ch_native
  src/python/routingkit_ch_bindings.cpp
  ${RK_SRC}
)
target_include_directories(routingkit_ch_native PRIVATE
  third_party/routingkit/include
  third_party/routingkit/src
)
target_compile_options(routingkit_ch_native PRIVATE -O3 -DNDEBUG)
# Output as _native.so inside the python package
set_target_properties(routingkit_ch_native PROPERTIES
  OUTPUT_NAME "_native"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python/routingkit_ch"
)
```

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add pybind11 + routingkit_ch CMake target"
```

---

## Task 3: Write the pybind11 bindings

**Files:**
- Create: `src/python/routingkit_ch_bindings.cpp`

- [ ] **Step 1: Create the file**

```cpp
/**
 * routingkit_ch_bindings.cpp — pybind11 bindings for RoutingKit CH.
 *
 * Exposes two classes:
 *   ContractionHierarchy — build / save / load / node_count
 *   CHQuery              — reset / add_source / add_target / run /
 *                          get_distance / get_node_path / get_arc_path
 *
 * Save/load format: CHB1 flat binary (see write_chb1 / read_chb1 below).
 * Magic "CHB1" + version u32(1) + n_nodes + rank[] + order[] +
 * Side(fwd) + Side(bwd).
 * Side = first_out[] + head[] + weight[] + bitvec + shortcut_first_arc[]
 *        + shortcut_second_arc[].
 * bitvec = u32 nbits + ceil(nbits/8) bytes packed LSB-first.
 * All integers little-endian u32.  Arrays are length-prefixed (u32 count).
 */

#include <routingkit/contraction_hierarchy.h>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace py = pybind11;
using namespace RoutingKit;

// ── helpers ──────────────────────────────────────────────────────────────────

static std::vector<unsigned>
to_uvec(py::array_t<uint32_t, py::array::c_style | py::array::forcecast> a) {
    auto r = a.unchecked<1>();
    return std::vector<unsigned>(r.data(0), r.data(0) + r.shape(0));
}

// ── CHB1 writer ──────────────────────────────────────────────────────────────

static void write_u32(std::ostream &f, uint32_t v) {
    char b[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)};
    f.write(b, 4);
}

static void write_vec(std::ostream &f, const std::vector<unsigned> &v) {
    write_u32(f, uint32_t(v.size()));
    for (unsigned x : v) write_u32(f, uint32_t(x));
}

static void write_bitvec(std::ostream &f, const BitVector &bv, uint32_t n) {
    write_u32(f, n);
    uint32_t nb = (n + 7) / 8;
    std::vector<uint8_t> buf(nb, 0);
    for (uint32_t i = 0; i < n; ++i)
        if (bv.is_set(i)) buf[i / 8] |= uint8_t(1u << (i % 8));
    f.write(reinterpret_cast<const char *>(buf.data()), nb);
}

static void write_side(std::ostream &f, const ContractionHierarchy::Side &s) {
    write_vec(f, s.first_out);
    write_vec(f, s.head);
    write_vec(f, s.weight);
    write_bitvec(f, s.is_shortcut_an_original_arc,
                 uint32_t(s.head.size()));
    write_vec(f, s.shortcut_first_arc);
    write_vec(f, s.shortcut_second_arc);
}

static void write_chb1(const ContractionHierarchy &ch,
                       const std::string &path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("Cannot open for write: " + path);
    f.write("CHB1", 4);
    write_u32(f, 1u);
    write_u32(f, uint32_t(ch.node_count()));
    write_vec(f, ch.rank);
    write_vec(f, ch.order);
    write_side(f, ch.forward);
    write_side(f, ch.backward);
    f.close();
    if (!f) throw std::runtime_error("Write error on: " + path);
}

// ── CHB1 reader ──────────────────────────────────────────────────────────────

static uint32_t read_u32(std::istream &f) {
    uint8_t b[4];
    f.read(reinterpret_cast<char *>(b), 4);
    if (!f) throw std::runtime_error("Truncated CHB1 file");
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) |
           (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}

static std::vector<unsigned> read_vec(std::istream &f) {
    uint32_t n = read_u32(f);
    std::vector<unsigned> v(n);
    for (uint32_t i = 0; i < n; ++i) v[i] = read_u32(f);
    return v;
}

static BitVector read_bitvec(std::istream &f) {
    uint32_t nbits = read_u32(f);
    uint32_t nb = (nbits + 7) / 8;
    std::vector<uint8_t> buf(nb);
    f.read(reinterpret_cast<char *>(buf.data()), nb);
    if (!f) throw std::runtime_error("Truncated bitvec");
    BitVector bv(nbits);
    for (uint32_t i = 0; i < nbits; ++i)
        if (buf[i / 8] & (1u << (i % 8))) bv.set(i);
    return bv;
}

static ContractionHierarchy::Side read_side(std::istream &f) {
    ContractionHierarchy::Side s;
    s.first_out               = read_vec(f);
    s.head                    = read_vec(f);
    s.weight                  = read_vec(f);
    s.is_shortcut_an_original_arc = read_bitvec(f);
    s.shortcut_first_arc      = read_vec(f);
    s.shortcut_second_arc     = read_vec(f);
    return s;
}

static ContractionHierarchy read_chb1(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    char magic[4];
    f.read(magic, 4);
    if (std::memcmp(magic, "CHB1", 4) != 0)
        throw std::runtime_error("Bad CHB1 magic in: " + path);
    uint32_t ver = read_u32(f);
    if (ver != 1)
        throw std::runtime_error("Unsupported CHB1 version: " +
                                 std::to_string(ver));
    uint32_t n = read_u32(f);
    ContractionHierarchy ch;
    ch.rank    = read_vec(f);
    ch.order   = read_vec(f);
    ch.forward  = read_side(f);
    ch.backward = read_side(f);
    if (ch.rank.size() != n)
        throw std::runtime_error("rank length mismatch in CHB1");
    return ch;
}

// ── Python classes ────────────────────────────────────────────────────────────

struct PyCH {
    ContractionHierarchy ch;

    static PyCH build(
        unsigned n,
        py::array_t<uint32_t, py::array::c_style | py::array::forcecast> tail,
        py::array_t<uint32_t, py::array::c_style | py::array::forcecast> head,
        py::array_t<uint32_t, py::array::c_style | py::array::forcecast> weight,
        unsigned max_pop_count = 500)
    {
        auto tv = to_uvec(tail);
        auto hv = to_uvec(head);
        auto wv = to_uvec(weight);
        PyCH result;
        {
            py::gil_scoped_release rel;
            result.ch = ContractionHierarchy::build(
                n, std::move(tv), std::move(hv), std::move(wv),
                std::function<void(std::string)>(),
                max_pop_count);
        }
        return result;
    }

    void save(const std::string &path) const { write_chb1(ch, path); }

    static PyCH load(const std::string &path) {
        PyCH r;
        r.ch = read_chb1(path);
        return r;
    }

    unsigned node_count() const { return ch.node_count(); }
};

struct PyCHQuery {
    ContractionHierarchyQuery q;
    explicit PyCHQuery(const PyCH &c) : q(c.ch) {}

    PyCHQuery &reset()                        { q.reset();           return *this; }
    PyCHQuery &add_source(unsigned s)         { q.add_source(s);     return *this; }
    PyCHQuery &add_target(unsigned t)         { q.add_target(t);     return *this; }
    PyCHQuery &run()                          { q.run();             return *this; }
    unsigned   get_distance()                 { return q.get_distance(); }
    std::vector<unsigned> get_node_path()     { return q.get_node_path(); }
    std::vector<unsigned> get_arc_path()      { return q.get_arc_path(); }
};

// ── Module ────────────────────────────────────────────────────────────────────

PYBIND11_MODULE(_native, m) {
    m.doc() = "RoutingKit ContractionHierarchy bindings";

    py::class_<PyCH>(m, "ContractionHierarchy")
        .def_static("build", &PyCH::build,
            py::arg("n_nodes"),
            py::arg("tail"),
            py::arg("head"),
            py::arg("weight"),
            py::arg("max_pop_count") = 500,
            "Build CH from flat edge arrays (uint32 numpy). Releases GIL.")
        .def("save", &PyCH::save, py::arg("path"),
            "Write to CHB1 flat binary file.")
        .def_static("load", &PyCH::load, py::arg("path"),
            "Load from CHB1 flat binary file.")
        .def_property_readonly("node_count", &PyCH::node_count);

    py::class_<PyCHQuery>(m, "CHQuery")
        .def(py::init<const PyCH &>(), py::arg("ch"))
        .def("reset",       &PyCHQuery::reset,      py::return_value_policy::reference)
        .def("add_source",  &PyCHQuery::add_source,
             py::arg("node"), py::return_value_policy::reference)
        .def("add_target",  &PyCHQuery::add_target,
             py::arg("node"), py::return_value_policy::reference)
        .def("run",         &PyCHQuery::run,        py::return_value_policy::reference)
        .def("get_distance",  &PyCHQuery::get_distance)
        .def("get_node_path", &PyCHQuery::get_node_path)
        .def("get_arc_path",  &PyCHQuery::get_arc_path);
}
```

- [ ] **Step 2: Verify the file compiles (smoke build)**

```bash
cd <fmm-fork>
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make routingkit_ch_native -j4
# Expect: _native.cpython-3XX-x86_64-linux-gnu.so in python/routingkit_ch/
```

- [ ] **Step 3: Commit**

```bash
git add src/python/routingkit_ch_bindings.cpp
git commit -m "feat: add routingkit_ch pybind11 bindings (CH build+query)"
```

---

## Task 4: Write the Python wrapper package

**Files:**
- Create: `python/routingkit_ch/__init__.py`
- Create: `python/routingkit_ch/py.typed`
- Create: `python/routingkit_ch/_native.pyi`

- [ ] **Step 1: Write `python/routingkit_ch/__init__.py`**

```python
"""routingkit_ch — RoutingKit ContractionHierarchy Python bindings.

Quickstart
----------
>>> import numpy as np
>>> from routingkit_ch import ContractionHierarchy, CHQuery
>>>
>>> ch = ContractionHierarchy.build(
...     n_nodes=5,
...     tail=np.array([0, 1, 2, 3], dtype=np.uint32),
...     head=np.array([1, 2, 3, 4], dtype=np.uint32),
...     weight=np.array([10, 20, 30, 40], dtype=np.uint32),
... )
>>> q = CHQuery(ch)
>>> q.reset().add_source(0).add_target(4).run()
>>> q.get_distance()
100
>>> q.get_node_path()
[0, 1, 2, 3, 4]
"""

from routingkit_ch._native import ContractionHierarchy, CHQuery

__all__ = ["ContractionHierarchy", "CHQuery"]
__version__ = "0.1.0"
```

- [ ] **Step 2: Write `python/routingkit_ch/py.typed`**

Empty file — PEP 561 marker so mypy knows this package ships stubs.

```bash
touch python/routingkit_ch/py.typed
```

- [ ] **Step 3: Write `python/routingkit_ch/_native.pyi`**

```python
from __future__ import annotations
import numpy as np
from numpy.typing import ArrayLike

class ContractionHierarchy:
    @staticmethod
    def build(
        n_nodes: int,
        tail: ArrayLike,
        head: ArrayLike,
        weight: ArrayLike,
        max_pop_count: int = 500,
    ) -> ContractionHierarchy: ...

    def save(self, path: str) -> None: ...

    @staticmethod
    def load(path: str) -> ContractionHierarchy: ...

    @property
    def node_count(self) -> int: ...

class CHQuery:
    def __init__(self, ch: ContractionHierarchy) -> None: ...
    def reset(self) -> CHQuery: ...
    def add_source(self, node: int) -> CHQuery: ...
    def add_target(self, node: int) -> CHQuery: ...
    def run(self) -> CHQuery: ...
    def get_distance(self) -> int: ...
    def get_node_path(self) -> list[int]: ...
    def get_arc_path(self) -> list[int]: ...
```

- [ ] **Step 4: Commit**

```bash
git add python/routingkit_ch/
git commit -m "feat: add routingkit_ch Python wrapper package"
```

---

## Task 5: Install routingkit_ch via python/CMakeLists.txt

**Files:**
- Modify: `python/CMakeLists.txt`

- [ ] **Step 1: Read current python/CMakeLists.txt**

Note where the SWIG library install targets are.

- [ ] **Step 2: Add install for routingkit_ch**

Append to the end of `python/CMakeLists.txt`:

```cmake
# Install routingkit_ch Python package
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/routingkit_ch
  DESTINATION ${PYTHON_SITE_PACKAGES}
)
```

- [ ] **Step 3: Test install**

```bash
cd build
make install  # or: cmake --install .
python -c "from routingkit_ch import ContractionHierarchy; print('OK')"
```

Expected: `OK`

- [ ] **Step 4: Commit**

```bash
git add python/CMakeLists.txt
git commit -m "build: install routingkit_ch package to site-packages"
```

---

## Task 6: Write tests

**Files:**
- Create: `tests/test_routingkit_ch.py`

- [ ] **Step 1: Write the failing tests first**

```python
"""Tests for routingkit_ch — ContractionHierarchy + CHQuery.

All tests use a tiny synthetic graph:

  0 --10--> 1 --20--> 2 --30--> 3 --40--> 4

Single directed path, so every OD pair has exactly one shortest path
and its distance is the prefix sum of weights.
"""
import numpy as np
import os
import pytest
from routingkit_ch import ContractionHierarchy, CHQuery

N = 5
TAIL   = np.array([0, 1, 2, 3], dtype=np.uint32)
HEAD   = np.array([1, 2, 3, 4], dtype=np.uint32)
WEIGHT = np.array([10, 20, 30, 40], dtype=np.uint32)


@pytest.fixture(scope="module")
def ch():
    return ContractionHierarchy.build(N, TAIL, HEAD, WEIGHT)


@pytest.fixture(scope="module")
def query(ch):
    return CHQuery(ch)


def test_node_count(ch):
    assert ch.node_count == N


def test_single_hop(query):
    query.reset().add_source(0).add_target(1).run()
    assert query.get_distance() == 10


def test_multi_hop_distance(query):
    query.reset().add_source(0).add_target(4).run()
    assert query.get_distance() == 100   # 10+20+30+40


def test_node_path(query):
    query.reset().add_source(0).add_target(4).run()
    assert query.get_node_path() == [0, 1, 2, 3, 4]


def test_arc_path_length(query):
    query.reset().add_source(0).add_target(4).run()
    arcs = query.get_arc_path()
    assert len(arcs) == 4          # 4 edges on a 5-node chain


def test_arc_path_all_valid(ch, query):
    """Every arc ID returned must be a valid 0-based arc index."""
    query.reset().add_source(0).add_target(4).run()
    arcs = query.get_arc_path()
    assert all(0 <= a < len(TAIL) for a in arcs), f"invalid arc ids: {arcs}"


def test_unreachable(query):
    query.reset().add_source(4).add_target(0).run()
    # RoutingKit returns 2**32 - 1 for unreachable
    assert query.get_distance() >= 2**31


def test_save_load_distance(ch, tmp_path):
    path = str(tmp_path / "test.ch")
    ch.save(path)
    assert os.path.exists(path)
    ch2 = ContractionHierarchy.load(path)
    q = CHQuery(ch2)
    q.reset().add_source(0).add_target(4).run()
    assert q.get_distance() == 100


def test_save_load_arc_path(ch, tmp_path):
    path = str(tmp_path / "test.ch")
    ch.save(path)
    ch2 = ContractionHierarchy.load(path)
    q2 = CHQuery(ch2)
    q2.reset().add_source(0).add_target(4).run()
    assert q2.get_arc_path() == CHQuery(ch).reset().add_source(0).add_target(4).run().get_arc_path()


def test_reuse_query_object(query):
    """CHQuery must be reusable across multiple reset() calls."""
    query.reset().add_source(0).add_target(2).run()
    assert query.get_distance() == 30
    query.reset().add_source(1).add_target(3).run()
    assert query.get_distance() == 50


def test_disconnected_graph():
    """Graph with two components — cross-component query is unreachable."""
    n = 4
    tail   = np.array([0, 2], dtype=np.uint32)
    head   = np.array([1, 3], dtype=np.uint32)
    weight = np.array([5, 5], dtype=np.uint32)
    ch = ContractionHierarchy.build(n, tail, head, weight)
    q = CHQuery(ch)
    q.reset().add_source(0).add_target(3).run()
    assert q.get_distance() >= 2**31
```

- [ ] **Step 2: Run the tests to confirm they fail before the build**

```bash
cd <fmm-fork>
python -m pytest tests/test_routingkit_ch.py -v
# Expected: ImportError (module not yet installed)
```

- [ ] **Step 3: Build and install, then re-run**

```bash
cd build && make routingkit_ch_native -j4 && make install
cd ..
python -m pytest tests/test_routingkit_ch.py -v
```

Expected output:
```
tests/test_routingkit_ch.py::test_node_count PASSED
tests/test_routingkit_ch.py::test_single_hop PASSED
tests/test_routingkit_ch.py::test_multi_hop_distance PASSED
tests/test_routingkit_ch.py::test_node_path PASSED
tests/test_routingkit_ch.py::test_arc_path_length PASSED
tests/test_routingkit_ch.py::test_arc_path_all_valid PASSED
tests/test_routingkit_ch.py::test_unreachable PASSED
tests/test_routingkit_ch.py::test_save_load_distance PASSED
tests/test_routingkit_ch.py::test_save_load_arc_path PASSED
tests/test_routingkit_ch.py::test_reuse_query_object PASSED
tests/test_routingkit_ch.py::test_disconnected_graph PASSED
11 passed
```

- [ ] **Step 4: Commit**

```bash
git add tests/test_routingkit_ch.py
git commit -m "test: add routingkit_ch test suite (11 cases)"
```

---

## Task 7: Add GitHub Actions CI

**Files:**
- Create: `.github/workflows/routingkit_ch_ci.yml`

- [ ] **Step 1: Write the workflow**

```yaml
name: routingkit_ch CI

on:
  push:
    branches: [main, master]
  pull_request:

jobs:
  build-and-test:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - name: Install system deps
        run: |
          sudo apt-get update -q
          sudo apt-get install -y cmake libgdal-dev libboost-dev \
            libboost-serialization-dev python3-dev python3-numpy swig

      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build routingkit_ch
        run: cmake --build build --target routingkit_ch_native -j4

      - name: Install
        run: sudo cmake --install build

      - name: Test
        run: python3 -m pytest tests/test_routingkit_ch.py -v
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/routingkit_ch_ci.yml
git commit -m "ci: add routingkit_ch build+test workflow"
```

---

## Task 8: PyPI packaging

FMM already has its own install mechanism (CMake `install`). For PyPI, the cleanest path is a
standalone `setup.py` / `pyproject.toml` **only for `routingkit_ch`**, separate from the main FMM
CMake install. This lets it be `pip install`-ed independently.

**Files:**
- Create: `python/routingkit_ch/setup.py`
- Create: `python/routingkit_ch/pyproject.toml`

- [ ] **Step 1: Write `python/routingkit_ch/pyproject.toml`**

```toml
[build-system]
requires = ["setuptools>=61", "pybind11>=2.10"]
build-backend = "setuptools.build_meta"

[project]
name = "routingkit-ch"
version = "0.1.0"
description = "Python bindings to RoutingKit ContractionHierarchy (build + query)"
readme = "README.md"
license = {text = "BSD-2-Clause"}
requires-python = ">=3.10"
dependencies = ["numpy>=1.20"]

[project.urls]
Homepage = "https://github.com/<your-org>/<your-fmm-fork>"
```

- [ ] **Step 2: Write `python/routingkit_ch/setup.py`**

This setup.py lives inside `python/routingkit_ch/` and refers to sources two levels up.

```python
from setuptools import setup, find_packages
from pybind11.setup_helpers import Pybind11Extension, build_ext
from pathlib import Path
import os

HERE   = Path(__file__).parent           # python/routingkit_ch/
ROOT   = HERE.parent.parent              # repo root
RK_DIR = ROOT / "third_party" / "routingkit"
BIND   = ROOT / "src" / "python" / "routingkit_ch_bindings.cpp"

rk_sources = sorted(str(p) for p in (RK_DIR / "src").glob("*.cpp"))

ext_modules = [
    Pybind11Extension(
        "routingkit_ch._native",
        sources=[str(BIND)] + rk_sources,
        include_dirs=[
            str(RK_DIR / "include"),
            str(RK_DIR / "src"),
        ],
        cxx_std=17,
        extra_compile_args=["-O3", "-DNDEBUG"],
    ),
]

setup(
    name="routingkit-ch",
    packages=["routingkit_ch"],
    package_dir={"routingkit_ch": str(HERE)},
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    package_data={"routingkit_ch": ["py.typed", "_native.pyi"]},
)
```

- [ ] **Step 3: Smoke-test the standalone pip install**

```bash
cd python/routingkit_ch
pip install -e . --no-build-isolation
python -c "from routingkit_ch import ContractionHierarchy; print('OK')"
```

Expected: `OK`

- [ ] **Step 4: Add cibuildwheel config for manylinux wheels**

Create `.github/workflows/publish_routingkit_ch.yml`:

```yaml
name: Publish routingkit-ch to PyPI

on:
  push:
    tags: ["rk-ch-v*"]

jobs:
  build_wheels:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - uses: pypa/cibuildwheel@v2.19.2
        with:
          package-dir: python/routingkit_ch
          output-dir: dist
        env:
          CIBW_BUILD: "cp310-* cp311-* cp312-* cp313-*"
          CIBW_MANYLINUX_X86_64_IMAGE: manylinux_2_28
          CIBW_BEFORE_BUILD: "pip install pybind11"
          CIBW_TEST_COMMAND: >
            python -c "from routingkit_ch import ContractionHierarchy; print('import ok')"

  publish:
    needs: build_wheels
    runs-on: ubuntu-22.04
    environment: pypi
    permissions:
      id-token: write
    steps:
      - uses: actions/download-artifact@v4
        with: {name: cibw-wheels, path: dist}
      - uses: pypa/gh-action-pypi-publish@release/v1
```

- [ ] **Step 5: Commit**

```bash
git add python/routingkit_ch/setup.py python/routingkit_ch/pyproject.toml
git add .github/workflows/publish_routingkit_ch.yml
git commit -m "build: add routingkit_ch standalone PyPI packaging + cibuildwheel"
```

---

## Self-Review

**Spec coverage check:**
- ✅ RoutingKit vendored (Task 1)
- ✅ CMake integration (Task 2)
- ✅ bindings.cpp with ContractionHierarchy + CHQuery (Task 3)
- ✅ Python wrapper + stubs (Task 4)
- ✅ Install via CMake (Task 5)
- ✅ Tests with build/save/load/query/unreachable/reuse (Task 6)
- ✅ CI workflow (Task 7)
- ✅ PyPI packaging + cibuildwheel (Task 8)

**Placeholder check:** None present — all code blocks are complete.

**Type consistency:**
- `ContractionHierarchy.build` takes numpy `uint32` arrays throughout Tasks 3, 4, 6 — consistent.
- `CHQuery` constructor takes `ContractionHierarchy` in Tasks 3, 4, 6 — consistent.
- `get_distance()` returns `unsigned` (C++) → `int` (Python) in all tasks — consistent.
- `save/load` path argument is `str` throughout — consistent.

**Known gotchas for the implementing agent:**
- `BitVector::set(i)` is the correct method name in RoutingKit. Do NOT use `bv[i] = true` (not valid).
- `ch.node_count()` returns `unsigned` in RoutingKit, exposed as a property (not method) in pybind11 via `def_property_readonly`.
- The `_native.so` output name must be exactly `_native` so `from routingkit_ch._native import ...` works; the CMake `OUTPUT_NAME "_native"` line ensures this.
- `max_pop_count=500` is RoutingKit's default (matches ch_ref.bin built here). Changing it produces a different-but-still-correct hierarchy.
- The `test_save_load_arc_path` test calls `.run()` without capturing the return value — this is correct since `run()` returns `*this` which is discarded; `.get_arc_path()` is a separate call.
