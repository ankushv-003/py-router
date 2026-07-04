/**
 * routingkit_ch_bindings.cpp — pybind11 bindings for RoutingKit CH.
 *
 * Save/load format: RoutingKit native binary (ContractionHierarchy::save_file /
 * load_file). Files produced here can be loaded directly by any RoutingKit
 * application without conversion.
 */

#include <routingkit/contraction_hierarchy.h>

#include "ch_alt.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <cstdint>
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

    void save(const std::string &path) const { ch.save_file(path); }

    static PyCH load(const std::string &path) {
        PyCH r;
        r.ch = ContractionHierarchy::load_file(path);
        return r;
    }

    unsigned node_count() const { return ch.node_count(); }
};

struct PyCHQuery {
    ContractionHierarchyQuery q;
    const ContractionHierarchy& ch;          // read-only, pinned via keep_alive
    std::vector<unsigned> touched_fwd;
    uint64_t alt_bound = 0;
    bool alt_state_valid = false;
    explicit PyCHQuery(const PyCH &c) : q(c.ch), ch(c.ch) {}

    PyCHQuery &reset()               { alt_state_valid = false; q.reset(); return *this; }
    PyCHQuery &add_source(unsigned s){ q.add_source(s); return *this; }
    PyCHQuery &add_target(unsigned t){ q.add_target(t); return *this; }
    // A stock run() uses stall-on-demand, whose pruned tentative labels are
    // unsafe for ADGW harvesting. Invalidate the alt state so a bare run()
    // after query_for_alt (without an intervening reset) cannot be harvested —
    // mirrors the reference's stock-query entrypoint. See ch_alt.hpp.
    PyCHQuery &run()                 { alt_state_valid = false; q.run(); return *this; }
    unsigned   get_distance()        { return q.get_distance(); }
    std::vector<unsigned> get_node_path() { return q.get_node_path(); }
    std::vector<unsigned> get_arc_path()  { return q.get_arc_path(); }

    unsigned query_for_alt(unsigned src, unsigned dst, double cap) {
        return ch_alt::query_for_alt(q, ch, touched_fwd, alt_bound, alt_state_valid, src, dst, cap);
    }
    std::pair<std::vector<unsigned>, std::vector<unsigned>> via_candidates(unsigned max_n) {
        std::vector<unsigned> nodes, vlens;
        ch_alt::via_candidates(q, ch, touched_fwd, alt_bound, alt_state_valid, max_n, nodes, vlens);
        return {std::move(nodes), std::move(vlens)};
    }
    std::tuple<unsigned, std::vector<unsigned>, std::vector<unsigned>> path_via(unsigned via) {
        std::vector<unsigned> np, ap;
        unsigned d = ch_alt::path_via(q, ch, alt_state_valid, via, np, ap);
        return {d, std::move(np), std::move(ap)};
    }
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
            "Build CH from flat edge arrays (uint32 numpy or lists). Releases GIL.")
        .def("save", &PyCH::save, py::arg("path"),
            "Write to RoutingKit native binary file (loadable by any RoutingKit application).",
            py::call_guard<py::gil_scoped_release>())
        .def("save_file", &PyCH::save, py::arg("path"),
            "Write to RoutingKit native binary file (matches RoutingKit C++ API name).",
            py::call_guard<py::gil_scoped_release>())
        .def_static("load", &PyCH::load, py::arg("path"),
            "Load from RoutingKit native binary file.",
            py::call_guard<py::gil_scoped_release>())
        .def_static("load_file", &PyCH::load, py::arg("path"),
            "Load from RoutingKit native binary file (matches RoutingKit C++ API name).",
            py::call_guard<py::gil_scoped_release>())
        .def_property_readonly("node_count", &PyCH::node_count);

    py::class_<PyCHQuery>(m, "CHQuery")
        // keep_alive<1,2>: pin the ContractionHierarchy for the query's lifetime.
        // PyCHQuery::q holds a reference into PyCH::ch, so the CH must outlive it.
        .def(py::init<const PyCH &>(), py::arg("ch"), py::keep_alive<1, 2>(),
             "Create a query over the given CH. The CH is kept alive for the query's lifetime.")
        .def("reset",       &PyCHQuery::reset,
             py::return_value_policy::reference)
        .def("add_source",  &PyCHQuery::add_source,
             py::arg("node"), py::return_value_policy::reference)
        .def("add_target",  &PyCHQuery::add_target,
             py::arg("node"), py::return_value_policy::reference)
        .def("run",         &PyCHQuery::run,
             py::return_value_policy::reference,
             py::call_guard<py::gil_scoped_release>())
        .def("get_distance",  &PyCHQuery::get_distance)
        .def("get_node_path", &PyCHQuery::get_node_path,
             py::call_guard<py::gil_scoped_release>())
        .def("get_arc_path",  &PyCHQuery::get_arc_path,
             py::call_guard<py::gil_scoped_release>())
        .def("query_for_alt", &PyCHQuery::query_for_alt,
             py::arg("src"), py::arg("dst"), py::arg("cap"),
             py::call_guard<py::gil_scoped_release>(),
             "Run the no-stall bidirectional ADGW search. Returns primary distance "
             "(INF_WEIGHT if unreachable). Enables via_candidates/path_via.")
        .def("via_candidates", &PyCHQuery::via_candidates, py::arg("max_n"),
             "After query_for_alt: (nodes, via_lens) ascending by via_len, <= max_n.")
        .def("path_via", &PyCHQuery::path_via, py::arg("via"),
             "After query_for_alt: (dist, node_path, arc_path) for src->via->dst; "
             "dist==INF_WEIGHT and empty paths on failure.");

    m.attr("INF_WEIGHT") = py::int_(RoutingKit::inf_weight);
}
