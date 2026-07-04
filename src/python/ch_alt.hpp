/**
 * ch_alt.hpp — ADGW alternative-routing native helpers over RoutingKit CH.
 *
 * Direct port of the no-stall bidirectional search used for alternative-route
 * generation in hermes/pkg/ch_router/routingkit/routingkit.cc (functions
 * settle_no_stall, query_for_alt_impl, rk_via_candidates, rk_path_via_into).
 * The QueryWrapper/extern "C" scaffolding from that file is dropped here;
 * search state is taken and returned by reference instead.
 *
 * ContractionHierarchyQuery's members used below (forward_queue/backward_queue,
 * was_forward_pushed/was_backward_pushed, *_tentative_distance,
 * *_predecessor_node/arc, shortest_path_meeting_node, state) are deliberately
 * public in contraction_hierarchy.h — its `private:` is commented out.
 */
#pragma once
#include <routingkit/contraction_hierarchy.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace ch_alt {
using namespace RoutingKit;

// One no-stall bidirectional settle step. Mirrors RoutingKit's forward_settle_node
// (and forward_expand_upward_ch_arcs_of_node) MINUS the stall-on-demand check, so
// tentative labels stay exact for harvesting. The `d < inf_weight` guard on the
// push branch matches RoutingKit's own forward_expand_upward_ch_arcs_of_node:
// without it, arcs relaxing to/through inf_weight get pushed as if they were
// real tentative distances, corrupting the "exact label" invariant that
// via_candidates and path_via depend on.
// Port of settle_no_stall in hermes routingkit.cc.
inline void settle_no_stall(
    const ContractionHierarchy::Side& side,
    MinIDQueue& queue, TimestampFlags& was_pushed,
    std::vector<unsigned>& tent,
    std::vector<unsigned>& pred_node, std::vector<unsigned>& pred_arc,
    const TimestampFlags& other_pushed, const std::vector<unsigned>& other_tent,
    unsigned& shortest, unsigned& meeting,
    std::vector<unsigned>* touched)
{
    auto p = queue.pop();
    unsigned node = p.id, dist = p.key;
    if (other_pushed.is_set(node)) {
        unsigned total = dist + other_tent[node];
        if (total < shortest) { shortest = total; meeting = node; }
    }
    for (unsigned a = side.first_out[node]; a < side.first_out[node + 1]; ++a) {
        unsigned h = side.head[a];
        unsigned d = dist + side.weight[a];
        if (was_pushed.is_set(h)) {
            if (d < tent[h]) {
                queue.decrease_key({h, d});
                tent[h] = d; pred_node[h] = node; pred_arc[h] = a;
            }
        } else if (d < inf_weight) {
            queue.push({h, d});
            was_pushed.set(h);
            tent[h] = d; pred_node[h] = node; pred_arc[h] = a;
            if (touched) touched->push_back(h);
        }
    }
}

// No-stall bidirectional search bounded by cap×best. Port of query_for_alt_impl.
// Sets alt_bound / alt_state_valid; returns primary distance (inf_weight if unreachable).
inline unsigned query_for_alt(
    ContractionHierarchyQuery& q, const ContractionHierarchy& ch,
    std::vector<unsigned>& touched_fwd, uint64_t& alt_bound, bool& alt_state_valid,
    unsigned from, unsigned to, double cap)
{
    if (from >= ch.node_count() || to >= ch.node_count()) {
        alt_state_valid = false; return inf_weight;
    }
    q.reset().add_source(from).add_target(to);
    touched_fwd.clear();
    touched_fwd.push_back(ch.rank[from]);
    if (!(cap >= 1.0)) cap = 1.0;
    if (cap > 64.0)    cap = 64.0;
    unsigned shortest = inf_weight;
    unsigned meeting  = invalid_id;
    bool forward_next = true;
    for (;;) {
        bool f_done = q.forward_queue.empty();
        bool b_done = q.backward_queue.empty();
        if (shortest < inf_weight) {
            double bound = cap * static_cast<double>(shortest);
            if (!f_done && static_cast<double>(q.forward_queue.peek().key)  > bound) f_done = true;
            if (!b_done && static_cast<double>(q.backward_queue.peek().key) > bound) b_done = true;
        }
        if (f_done && b_done) break;
        if (f_done) forward_next = false;
        if (b_done) forward_next = true;
        if (forward_next) {
            settle_no_stall(ch.forward, q.forward_queue, q.was_forward_pushed,
                q.forward_tentative_distance, q.forward_predecessor_node, q.forward_predecessor_arc,
                q.was_backward_pushed, q.backward_tentative_distance, shortest, meeting, &touched_fwd);
            forward_next = false;
        } else {
            settle_no_stall(ch.backward, q.backward_queue, q.was_backward_pushed,
                q.backward_tentative_distance, q.backward_predecessor_node, q.backward_predecessor_arc,
                q.was_forward_pushed, q.forward_tentative_distance, shortest, meeting, nullptr);
            forward_next = true;
        }
    }
    q.shortest_path_meeting_node = meeting;
    q.state = ContractionHierarchyQuery::InternalState::run;
    alt_bound = (shortest < inf_weight)
        ? static_cast<uint64_t>(cap * static_cast<double>(shortest)) : ~uint64_t(0);
    alt_state_valid = true;
    return q.get_distance();
}

// Enumerate touched_fwd; keep nodes reached both ways with via_len <= alt_bound.
// Sorted ascending (via_len, node). Port of rk_via_candidates.
inline void via_candidates(
    ContractionHierarchyQuery& q, const ContractionHierarchy& ch,
    const std::vector<unsigned>& touched_fwd, uint64_t alt_bound, bool alt_state_valid,
    unsigned max_n, std::vector<unsigned>& out_nodes, std::vector<unsigned>& out_via_lens)
{
    out_nodes.clear(); out_via_lens.clear();
    if (!alt_state_valid) return;
    struct Cand { unsigned node, via_len; };
    std::vector<Cand> cands;
    for (unsigned r : touched_fwd) {
        if (!q.was_backward_pushed.is_set(r)) continue;
        uint64_t via = static_cast<uint64_t>(q.forward_tentative_distance[r])
                     + q.backward_tentative_distance[r];
        if (via > alt_bound) continue;
        cands.push_back({ch.order[r], static_cast<unsigned>(via)});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return a.via_len != b.via_len ? a.via_len < b.via_len : a.node < b.node;
    });
    for (const auto& c : cands) {
        if (out_nodes.size() >= max_n) break;
        out_nodes.push_back(c.node);
        out_via_lens.push_back(c.via_len);
    }
}

// Unpack src->via->dst from search state by forcing the meeting node.
// Port of rk_path_via_into. Returns via distance (inf_weight on failure).
inline unsigned path_via(
    ContractionHierarchyQuery& q, const ContractionHierarchy& ch, bool alt_state_valid,
    unsigned via, std::vector<unsigned>& node_path, std::vector<unsigned>& arc_path)
{
    node_path.clear(); arc_path.clear();
    if (!alt_state_valid || via >= ch.node_count()) return inf_weight;
    unsigned r = ch.rank[via];
    if (!q.was_forward_pushed.is_set(r))  return inf_weight;
    if (!q.was_backward_pushed.is_set(r)) return inf_weight;
    unsigned fwd = q.forward_tentative_distance[r];
    unsigned bwd = q.backward_tentative_distance[r];
    if (fwd >= inf_weight || bwd >= inf_weight) return inf_weight;
    unsigned saved = q.shortest_path_meeting_node;
    q.shortest_path_meeting_node = r;
    node_path = q.get_node_path();
    arc_path  = q.get_arc_path();
    q.shortest_path_meeting_node = saved;
    return fwd + bwd;
}

} // namespace ch_alt
