/**
 * routingkit_ch_bindings.cpp — pybind11 bindings for RoutingKit CH.
 *
 * Save/load format: CHB1 flat binary.
 * Magic "CHB1" + version u32(1) + n_nodes + rank[] + order[] +
 * Side(fwd) + Side(bwd).
 * Side = first_out[] + head[] + weight[] + bitvec + shortcut_first_arc[]
 *        + shortcut_second_arc[].
 * bitvec = u32 nbits + ceil(nbits/8) bytes packed LSB-first.
 * All integers little-endian u32. Arrays are length-prefixed (u32 count).
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
    write_bitvec(f, s.is_shortcut_an_original_arc, uint32_t(s.head.size()));
    write_vec(f, s.shortcut_first_arc);
    write_vec(f, s.shortcut_second_arc);
}

static void write_chb1(const ContractionHierarchy &ch, const std::string &path) {
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
    s.first_out                   = read_vec(f);
    s.head                        = read_vec(f);
    s.weight                      = read_vec(f);
    s.is_shortcut_an_original_arc = read_bitvec(f);
    s.shortcut_first_arc          = read_vec(f);
    s.shortcut_second_arc         = read_vec(f);
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
    ch.rank     = read_vec(f);
    ch.order    = read_vec(f);
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

    PyCHQuery &reset()               { q.reset();       return *this; }
    PyCHQuery &add_source(unsigned s){ q.add_source(s); return *this; }
    PyCHQuery &add_target(unsigned t){ q.add_target(t); return *this; }
    PyCHQuery &run()                 { q.run();         return *this; }
    unsigned   get_distance()        { return q.get_distance(); }
    std::vector<unsigned> get_node_path() { return q.get_node_path(); }
    std::vector<unsigned> get_arc_path()  { return q.get_arc_path(); }
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
            "Write to CHB1 flat binary file.")
        .def_static("load", &PyCH::load, py::arg("path"),
            "Load from CHB1 flat binary file.")
        .def_property_readonly("node_count", &PyCH::node_count);

    py::class_<PyCHQuery>(m, "CHQuery")
        .def(py::init<const PyCH &>(), py::arg("ch"))
        .def("reset",       &PyCHQuery::reset,
             py::return_value_policy::reference)
        .def("add_source",  &PyCHQuery::add_source,
             py::arg("node"), py::return_value_policy::reference)
        .def("add_target",  &PyCHQuery::add_target,
             py::arg("node"), py::return_value_policy::reference)
        .def("run",         &PyCHQuery::run,
             py::return_value_policy::reference)
        .def("get_distance",  &PyCHQuery::get_distance)
        .def("get_node_path", &PyCHQuery::get_node_path)
        .def("get_arc_path",  &PyCHQuery::get_arc_path);
}
