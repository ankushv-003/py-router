"""Regression: concurrent FastMapMatch(cache_dir=...) on one cache_dir.

Before the fix, the constructor checked exists() and then wrote the UBODT straight to its final path. A second
process starting mid-write saw the partial file and failed ("input stream error"), or both regenerated it.
Now one process generates under <file>.lock into a temp name renamed into place; the others wait and reuse it.
"""

import glob
import os
import subprocess
import sys
import textwrap

_BUILD = textwrap.dedent('''
    import sys
    from fastmm import FastMapMatch, Network, Trajectory, TransitionMode
    N, S = int(sys.argv[2]), 100.0
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
    m = FastMapMatch(net, TransitionMode.SHORTEST, max_distance_between_candidates=3000.0, cache_dir=sys.argv[1])
    r = m.match(Trajectory.from_xy_tuples([((c + 0.3) * S, 2 * S + 5) for c in range(2, 12)]),
                max_candidates=8, candidate_search_radius=50, gps_error=50)
    sys.exit(0 if all(str(s.error_code).endswith("SUCCESS") for s in r.subtrajectories) else 3)
''')


def _round(tmp_path):
    procs = [subprocess.Popen([sys.executable, "-c", _BUILD, str(tmp_path), "60"],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) for _ in range(6)]
    outs = [p.communicate(timeout=600)[0] for p in procs]
    for p, out in zip(procs, outs):
        assert p.returncode == 0, f"rc={p.returncode}: {out[-800:]}"
    assert sum(out.count("Generating UBODT") for out in outs) == 1, "UBODT generated more than once"
    assert len(glob.glob(os.path.join(tmp_path, "ubodt_*.bin"))) == 1
    assert not glob.glob(os.path.join(tmp_path, "*.tmp.*")), "temp file left behind"


def test_concurrent_generation_into_one_cache_dir(tmp_path):
    for k in range(2):  # the unfixed race shows up in most single rounds; two rounds make it near-certain
        d = tmp_path / f"round{k}"
        d.mkdir()
        _round(d)
