import ch_router

# Diamond graph: 0->1 (1), 0->2 (5), 1->3 (1), 2->3 (1); plus a 3rd route
# 0->4 (2), 4->3 (2) giving an alternative of length 4 vs optimal 2.
TAILS = [0, 0, 1, 2, 0, 4]
HEADS = [1, 2, 3, 3, 4, 3]
WTS   = [1, 5, 1, 1, 2, 2]
N = 5

def _ch():
    return ch_router.ContractionHierarchy.build(N, TAILS, HEADS, WTS)

def test_query_for_alt_matches_stock_primary_distance():
    ch = _ch()
    q = ch_router.CHQuery(ch)
    # stock
    q.reset(); q.add_source(0); q.add_target(3); q.run()
    stock = q.get_distance()
    # adgw
    d = q.query_for_alt(0, 3, 1.8)
    assert d == stock == 2   # 0-1-3

def test_via_candidates_nonempty_and_sorted():
    ch = _ch()
    q = ch_router.CHQuery(ch)
    q.query_for_alt(0, 3, 3.0)   # loose cap so node 4 (via_len 4) qualifies
    nodes, vlens = q.via_candidates(256)
    assert len(nodes) == len(vlens) >= 1
    assert vlens == sorted(vlens)   # ascending by via_len

def test_path_via_distance_equals_via_len():
    ch = _ch()
    q = ch_router.CHQuery(ch)
    q.query_for_alt(0, 3, 3.0)
    nodes, vlens = q.via_candidates(256)
    for v, vl in zip(nodes, vlens):
        d, npath, apath = q.path_via(v)
        assert d == vl
        assert npath[0] == 0 and npath[-1] == 3   # src..dst

def test_harvest_gated_after_stock_run():
    ch = _ch()
    q = ch_router.CHQuery(ch)
    q.reset(); q.add_source(0); q.add_target(3); q.run()   # stock, not query_for_alt
    nodes, vlens = q.via_candidates(256)
    assert nodes == [] and vlens == []   # alt_state_valid must be false

def test_harvest_gated_after_bare_run_following_alt():
    # A bare run() AFTER a query_for_alt (no intervening reset) must invalidate
    # the alt state: run()'s stall-pruned labels are unsafe to harvest.
    ch = _ch()
    q = ch_router.CHQuery(ch)
    q.query_for_alt(0, 3, 3.0)
    assert len(q.via_candidates(256)[0]) >= 1      # harvest works right after alt
    q.add_source(0); q.add_target(3); q.run()      # bare stock run, no reset
    nodes, vlens = q.via_candidates(256)
    assert nodes == [] and vlens == []             # run() cleared alt_state_valid
