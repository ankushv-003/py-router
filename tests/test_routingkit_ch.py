"""Tests for routingkit_ch — ContractionHierarchy + CHQuery.

Graph used in most tests:

  0 --10--> 1 --20--> 2 --30--> 3 --40--> 4

Single directed chain, so every reachable OD pair has exactly one
shortest path and its distance is the prefix sum of weights.
"""
import os
import numpy as np
import pytest

try:
    from routingkit_ch import ContractionHierarchy, CHQuery
    HAVE_RK = True
except ImportError:
    HAVE_RK = False

pytestmark = pytest.mark.skipif(not HAVE_RK, reason="routingkit_ch not built yet")

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
    assert query.get_distance() == 100  # 10+20+30+40


def test_node_path(query):
    query.reset().add_source(0).add_target(4).run()
    assert query.get_node_path() == [0, 1, 2, 3, 4]


def test_arc_path_length(query):
    query.reset().add_source(0).add_target(4).run()
    assert len(query.get_arc_path()) == 4


def test_arc_path_all_valid(query):
    query.reset().add_source(0).add_target(4).run()
    arcs = query.get_arc_path()
    assert all(0 <= a < len(TAIL) for a in arcs), f"invalid arc ids: {arcs}"


def test_unreachable(query):
    query.reset().add_source(4).add_target(0).run()
    assert query.get_distance() >= 2**31 - 1


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
    original = CHQuery(ch)
    original.reset().add_source(0).add_target(4).run()
    assert q2.get_arc_path() == original.get_arc_path()


def test_reuse_query_object(query):
    query.reset().add_source(0).add_target(2).run()
    assert query.get_distance() == 30
    query.reset().add_source(1).add_target(3).run()
    assert query.get_distance() == 50


def test_disconnected_graph():
    n = 4
    tail   = np.array([0, 2], dtype=np.uint32)
    head   = np.array([1, 3], dtype=np.uint32)
    weight = np.array([5, 5], dtype=np.uint32)
    ch = ContractionHierarchy.build(n, tail, head, weight)
    q = CHQuery(ch)
    q.reset().add_source(0).add_target(3).run()
    assert q.get_distance() >= 2**31 - 1
