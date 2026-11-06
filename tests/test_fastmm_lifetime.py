"""Regression: FastMapMatch must keep its Network alive.

FastMapMatch holds a C++ reference to the Network. Without py::keep_alive<1, 2>() on its constructors, a matcher
that outlives its Network (e.g. built in a helper that returns only the matcher) reads freed memory: SIGSEGV/SIGBUS
in Network::search_tr_cs_knn, "Spatial index not built", or a silent all-failed match.
"""

import subprocess
import sys
import textwrap

from fastmm import FastMapMatch, Network, NetworkGraph, TransitionMode, UBODT, UBODTGenAlgorithm


def _line_net():
    net = Network()
    net.add_edge(1, source=10, target=20, geom=[(0.0, 0.0), (100.0, 0.0)])
    net.add_edge(2, source=20, target=30, geom=[(100.0, 0.0), (200.0, 0.0)])
    net.finalize()
    return net


def test_ubodt_constructor_pins_network(tmp_path):
    net = _line_net()
    f = tmp_path / "u.bin"
    UBODTGenAlgorithm(net, NetworkGraph(net, TransitionMode.SHORTEST), TransitionMode.SHORTEST).generate_ubodt(
        str(f), delta=500.0, network_hash=net.compute_hash() if hasattr(net, "compute_hash") else "")
    ub = UBODT.read_ubodt(str(f))
    before = sys.getrefcount(net)
    m = FastMapMatch(net, TransitionMode.SHORTEST, ub)
    assert sys.getrefcount(net) == before + 1
    del m


def test_cache_dir_constructor_pins_network(tmp_path):
    net = _line_net()
    before = sys.getrefcount(net)
    m = FastMapMatch(net, TransitionMode.SHORTEST, max_distance_between_candidates=500.0, cache_dir=str(tmp_path))
    assert sys.getrefcount(net) == before + 1
    del m


# End-to-end: match after the only Python reference to the Network is gone, with its memory reused.
# Runs in a subprocess because the unfixed failure mode is a native crash.
_DROPPED_NET = textwrap.dedent('''
    import gc, sys
    from fastmm import FastMapMatch, Network, Trajectory, TransitionMode
    N, S = 30, 100.0
    def make(cache):
        net = Network(); e = 0
        for i in range(N):
            for j in range(N):
                for di, dj in ((0, 1), (1, 0)):
                    ii, jj = i + di, j + dj
                    if ii < N and jj < N:
                        a, b = (j * S, i * S), (jj * S, ii * S)
                        net.add_edge(e, source=i * N + j, target=ii * N + jj, geom=[a, b]); e += 1
                        net.add_edge(e, source=ii * N + jj, target=i * N + j, geom=[b, a]); e += 1
        net.finalize()
        return FastMapMatch(net, TransitionMode.SHORTEST, max_distance_between_candidates=1000.0, cache_dir=cache)
    m = make(sys.argv[1])
    for _ in range(3):
        gc.collect()
    junk = [bytearray(1 << 20) for _ in range(64)]
    trajs = [Trajectory.from_xy_tuples([((c + 0.3) * S, (2 + t % 20) * S + 5) for c in range(2, 25)]) for t in range(200)]
    res = m.match_many(trajs, max_candidates=8, candidate_search_radius=50, gps_error=50, workers=4)
    ok = sum(1 for r in res for s in r.subtrajectories if str(s.error_code).endswith("SUCCESS"))
    print(ok)
    sys.exit(0 if ok == len(trajs) else 3)
''')


def test_match_after_network_dropped(tmp_path):
    for _ in range(3):
        r = subprocess.run([sys.executable, "-c", _DROPPED_NET, str(tmp_path)], capture_output=True, text=True, timeout=300)
        assert r.returncode == 0, f"rc={r.returncode} out={r.stdout[-300:]} err={r.stderr[-500:]}"
