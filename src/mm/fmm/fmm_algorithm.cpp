//
// Created by Can Yang on 2020/3/22.
//

#include "mm/fmm/fmm_algorithm.hpp"
#include "mm/fmm/ubodt_gen_algorithm.hpp"
#include "algorithm/geom_algorithm.hpp"
#include "util/util.hpp"
#include "util/debug.hpp"

#include <filesystem>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>

using namespace FASTMM;
using namespace FASTMM::CORE;
using namespace FASTMM::NETWORK;
using namespace FASTMM::MM;

FastMapMatch::FastMapMatch(const Network &network,
                           TransitionMode mode,
                           std::optional<double> max_distance_between_candidates,
                           std::optional<double> max_time_between_candidates,
                           const std::string &cache_dir)
    : network_(network), graph_(network, mode), ubodt_(nullptr), mode_(mode)
{
    // Validate inputs
    double delta = 0.0;
    if (mode == TransitionMode::SHORTEST)
    {
        if (!max_distance_between_candidates.has_value() || max_distance_between_candidates.value() <= 0)
        {
            throw std::invalid_argument("FastMapMatch: max_distance_between_candidates must be positive for SHORTEST mode");
        }
        delta = max_distance_between_candidates.value();
    }
    else if (mode == TransitionMode::FASTEST)
    {
        if (!max_time_between_candidates.has_value() || max_time_between_candidates.value() <= 0)
        {
            throw std::invalid_argument("FastMapMatch: max_time_between_candidates must be positive for FASTEST mode");
        }
        delta = max_time_between_candidates.value();
    }
    else
    {
        throw std::invalid_argument("FastMapMatch: Unknown transition mode");
    }
    if (!network_.is_finalized())
    {
        throw std::invalid_argument("FastMapMatch: Network must be finalized (call finalize() first)");
    }
    if (network_.get_edge_count() == 0)
    {
        throw std::invalid_argument("FastMapMatch: Network contains no edges");
    }

    // Create cache directory
    std::filesystem::path cache_path(cache_dir);
    std::filesystem::create_directories(cache_path);

    // Compute network hash for cache validation
    std::string network_hash = network_.compute_hash();

    // Generate cache filename based on network hash, mode, and delta
    std::ostringstream filename;
    filename << "ubodt_" << network_hash << "_"
             << (mode == TransitionMode::SHORTEST ? "shortest" : "fastest")
             << "_delta" << std::fixed << std::setprecision(1) << delta << ".bin";
    std::filesystem::path ubodt_path = cache_path / filename.str();

    SPDLOG_INFO("FastMapMatch: mode={}, delta={}, cache={}",
                (mode == TransitionMode::SHORTEST ? "SHORTEST" : "FASTEST"),
                delta, ubodt_path.string());

    // Generate or load UBODT
    if (!std::filesystem::exists(ubodt_path))
    {
        SPDLOG_INFO("Generating UBODT and saving to {}", ubodt_path.string());
        SPDLOG_INFO("This may take a while for large networks...");

        UBODTGenAlgorithm ubodt_gen(network_, graph_, mode);
        ubodt_gen.generate_ubodt(ubodt_path.string(), delta, network_hash);
    }
    else
    {
        SPDLOG_INFO("Found cached UBODT at {}", ubodt_path.string());
    }

    // Load UBODT and validate
    SPDLOG_INFO("Loading UBODT from {}", ubodt_path.string());
    ubodt_ = UBODT::read_ubodt(ubodt_path.string());

    // Verify loaded UBODT metadata
    std::string loaded_hash = ubodt_->get_network_hash();
    TransitionMode loaded_mode = ubodt_->get_mode();
    double loaded_delta = ubodt_->get_delta();
    int loaded_num_vertices = ubodt_->get_num_vertices();
    long long loaded_num_rows = ubodt_->get_num_rows();

    SPDLOG_INFO("Loaded UBODT: hash={}, mode={}, delta={} with {} vertices",
                loaded_hash,
                (loaded_mode == TransitionMode::SHORTEST ? "SHORTEST" : "FASTEST"),
                loaded_delta,
                loaded_num_vertices);

    // Validate num vertices:
    if (loaded_num_rows == 0 || loaded_num_vertices == 0)
    {
        throw std::runtime_error("Loaded UBODT is empty!");
    }
    if (loaded_num_vertices < network_.get_node_count())
    {
        throw std::runtime_error("Loaded UBODT has fewer vertices than network nodes!");
    }

    // Validate mode
    if (loaded_mode != mode)
    {
        throw std::runtime_error(
            "UBODT mode mismatch! Expected " +
            std::string(mode == TransitionMode::SHORTEST ? "SHORTEST" : "FASTEST") +
            " but UBODT was generated with " +
            std::string(loaded_mode == TransitionMode::SHORTEST ? "SHORTEST" : "FASTEST") +
            ". Please delete " + ubodt_path.string() + " to regenerate.");
    }

    // Validate network hash
    if (loaded_hash.empty() || loaded_hash != network_hash)
    {
        throw std::runtime_error(
            "UBODT network hash mismatch! Expected " + network_hash +
            " but UBODT has " + loaded_hash +
            ". The network has changed. Please delete " + ubodt_path.string() + " to regenerate.");
    }

    // Validate delta:
    if (std::abs(loaded_delta - delta) > 1e-6)
    {
        throw std::runtime_error(
            "UBODT delta mismatch! Expected " + std::to_string(delta) +
            " but UBODT has " + std::to_string(loaded_delta) +
            ". Please delete " + ubodt_path.string() + " to regenerate.");
    }

    SPDLOG_INFO("FastMapMatch initialized successfully.");
}

FastMapMatch::FastMapMatch(const Network &network,
                           TransitionMode mode,
                           std::shared_ptr<UBODT> ubodt)
    : network_(network), graph_(network, mode), ubodt_(std::move(ubodt)), mode_(mode)
{
    if (!network_.is_finalized())
    {
        throw std::invalid_argument("FastMapMatch: Network must be finalized (call finalize() first)");
    }
    if (network_.get_edge_count() == 0)
    {
        throw std::invalid_argument("FastMapMatch: Network contains no edges");
    }
    if (!ubodt_)
    {
        throw std::invalid_argument("FastMapMatch: ubodt must not be null");
    }

    std::string network_hash = network_.compute_hash();
    std::string loaded_hash = ubodt_->get_network_hash();
    TransitionMode loaded_mode = ubodt_->get_mode();
    int loaded_num_vertices = ubodt_->get_num_vertices();
    long long loaded_num_rows = ubodt_->get_num_rows();

    if (loaded_num_rows == 0 || loaded_num_vertices == 0)
    {
        throw std::runtime_error("Provided UBODT is empty!");
    }
    if (loaded_num_vertices < network_.get_node_count())
    {
        throw std::runtime_error("Provided UBODT has fewer vertices than network nodes!");
    }
    if (loaded_mode != mode)
    {
        throw std::runtime_error(
            "UBODT mode mismatch! Expected " +
            std::string(mode == TransitionMode::SHORTEST ? "SHORTEST" : "FASTEST") +
            " but UBODT was generated with " +
            std::string(loaded_mode == TransitionMode::SHORTEST ? "SHORTEST" : "FASTEST"));
    }
    if (loaded_hash.empty() || loaded_hash != network_hash)
    {
        throw std::runtime_error(
            "UBODT network hash mismatch! Expected " + network_hash +
            " but UBODT has " + loaded_hash +
            ". The network has changed since the UBODT was generated.");
    }

    SPDLOG_INFO("FastMapMatch initialized with pre-loaded UBODT.");
}

FastMapMatchConfig::FastMapMatchConfig(int max_candidates,
                                       double candidate_search_radius,
                                       double gps_error,
                                       double reverse_tolerance,
                                       TransitionMode transition_mode,
                                       std::optional<double> reference_speed,
                                       double max_route_distance_factor,
                                       double turn_penalty_factor)
    : max_candidates(max_candidates),
      candidate_search_radius(candidate_search_radius),
      gps_error(gps_error),
      reverse_tolerance(reverse_tolerance),
      transition_mode(transition_mode),
      reference_speed(reference_speed),
      max_route_distance_factor(max_route_distance_factor),
      turn_penalty_factor(turn_penalty_factor)
{
    // Validation
    if (transition_mode == TransitionMode::FASTEST && !reference_speed.has_value())
    {
        throw std::invalid_argument("Reference speed is required for FASTEST mode");
    }
    if (reference_speed.has_value() && reference_speed.value() <= 0)
    {
        throw std::invalid_argument("Reference speed must be positive");
    }
};

MatchResult FastMapMatch::match_trajectory(const Trajectory &trajectory, const FastMapMatchConfig &config)
{
    if (config.transition_mode != ubodt_->get_mode() || config.transition_mode != mode_)
    {
        throw std::invalid_argument("FastMapMatch::match_trajectory: Transition mode in config does not match FastMapMatch mode");
    }

    SPDLOG_DEBUG("Count of points in trajectory {}", trajectory.geom.get_num_points());
    SPDLOG_DEBUG("Search candidates");
    TrajectoryCandidates tc = network_.search_tr_cs_knn(trajectory.geom, config.max_candidates, config.candidate_search_radius);
    SPDLOG_DEBUG("Trajectory candidate {}", tc);
    MatchResult result = MatchResult{};
    result.error_code = MatchErrorCode::UNKNOWN_ERROR;
    std::vector<int> unmatched_indices;
    for (int i = 0; i < tc.size(); ++i)
    {
        if (tc[i].empty())
        {
            unmatched_indices.push_back(i);
        }
    }

    if (!unmatched_indices.empty())
    {
        SPDLOG_DEBUG("No candidates found for trajectory at points {}", unmatched_indices);
        result.error_code = MatchErrorCode::CANDIDATES_NOT_FOUND;
        return result;
    }

    SPDLOG_DEBUG("Generate transition graph");
    TransitionGraph tg(tc, config.gps_error);
    SPDLOG_DEBUG("Update cost in transition graph");
    // The network will be used internally to update transition graph
    bool all_connected = false;
    int last_connected = update_tg(&tg, trajectory, config, &all_connected);
    if (!all_connected)
    {
        SPDLOG_DEBUG("Traj unmatched at trajectory point {}", last_connected);
        result.error_code = MatchErrorCode::DISCONNECTED_LAYERS;
        return result;
    }
    SPDLOG_DEBUG("Optimal path inference");
    TGOpath tg_opath = tg.backtrack();
    SPDLOG_DEBUG("Optimal path size {}", tg_opath.size());
    MatchedCandidatePath matched_candidate_path(tg_opath.size());
    std::transform(tg_opath.begin(), tg_opath.end(), matched_candidate_path.begin(), [](const TGNode *a)
                   { return MatchedCandidate{
                         *(a->c), a->ep, a->tp, a->shortest_path_distance}; });
    OptimalPath optimal_path(tg_opath.size());
    std::transform(tg_opath.begin(), tg_opath.end(), optimal_path.begin(), [](const TGNode *a)
                   { return a->c->edge->id; });
    std::vector<int> indices;
    const std::vector<Edge> &edges = network_.get_edges();
    CompletePath complete_path = ubodt_->construct_complete_path(tg_opath, edges, &indices, config.reverse_tolerance);
    SPDLOG_DEBUG("Opath is {}", optimal_path);
    SPDLOG_DEBUG("Indices is {}", indices);
    SPDLOG_DEBUG("Complete path is {}", complete_path);
    LineString matched_geometry = network_.complete_path_to_geometry(trajectory.geom, complete_path);

    result.error_code = MatchErrorCode::SUCCESS;
    result.opt_candidate_path = matched_candidate_path;
    result.optimal_path = optimal_path;
    result.complete_path = complete_path;
    result.indices = indices;
    result.matched_geometry = matched_geometry;
    return result;
}

double FastMapMatch::get_distance(const Candidate *ca, const Candidate *cb, double reverse_tolerance)
{
    double distance = 0;
    if (ca->edge->id == cb->edge->id && ca->offset <= cb->offset)
    {
        // Transition on the same edge, where b is after a i.e. not reversing.
        distance = cb->offset - ca->offset;
    }
    else if (ca->edge->id == cb->edge->id && ca->offset - cb->offset < reverse_tolerance)
    {
        // Transition on the same edge, where b is before a but also within the reverse tolerance, then allow.
        distance = 0;
    }
    else if (ca->edge->target == cb->edge->source)
    {
        // Transition on the same OD nodes
        distance = ca->edge->length - ca->offset + cb->offset;
    }
    else
    {
        const Record *r = ubodt_->look_up(ca->edge->target, cb->edge->source);
        // No sp path exist from O to D.
        if (r == nullptr)
            return std::numeric_limits<double>::infinity();
        // UBODT stores cost (distance for SHORTEST mode, time for FASTEST mode)
        distance = r->cost + ca->edge->length - ca->offset + cb->offset;
    }
    return distance;
}

double FastMapMatch::get_time(const Candidate *ca, const Candidate *cb, double reverse_tolerance)
{
    double time = 0;
    if (ca->edge->id == cb->edge->id && ca->offset <= cb->offset)
    {
        // Transition on the same edge
        // Speed is guaranteed to exist since NetworkGraph constructor validates this in FASTEST mode
        double segment_distance = cb->offset - ca->offset;
        time = segment_distance / ca->edge->speed;
    }
    else if (ca->edge->id == cb->edge->id && ca->offset - cb->offset < reverse_tolerance)
    {
        // Reverse within tolerance
        time = 0;
    }
    else if (ca->edge->target == cb->edge->source)
    {
        // Transition on adjacent edges
        // Speed is guaranteed to exist since NetworkGraph constructor validates this in FASTEST mode
        double ca_remaining = ca->edge->length - ca->offset;
        double cb_initial = cb->offset;

        double time_ca = ca_remaining / ca->edge->speed;
        double time_cb = cb_initial / cb->edge->speed;
        time = time_ca + time_cb;
    }
    else
    {
        const Record *r = ubodt_->look_up(ca->edge->target, cb->edge->source);
        if (r == nullptr)
            return std::numeric_limits<double>::infinity();

        // Calculate time on ca and cb edges
        // Speed is guaranteed to exist since NetworkGraph constructor validates this in FASTEST mode
        double ca_remaining = ca->edge->length - ca->offset;
        double cb_initial = cb->offset;

        double time_ca = ca_remaining / ca->edge->speed;
        double time_cb = cb_initial / cb->edge->speed;

        // UBODT cost: for FASTEST mode it should be time, for SHORTEST it's distance
        // This is a known limitation - ideally UBODT should be mode-specific
        time = time_ca + r->cost + time_cb;
    }
    return time;
}

int FastMapMatch::update_tg(TransitionGraph *tg, const Trajectory &trajectory, const FastMapMatchConfig &config, bool *all_connected)
{
    SPDLOG_DEBUG("Update transition graph");
    std::vector<TGLayer> &layers = tg->get_layers();
    std::vector<double> euclidean_distances = ALGORITHM::calculate_linestring_euclidean_distances(trajectory.geom);
    int N = layers.size();

    for (int i = 0; i < N - 1; ++i)
    {
        SPDLOG_DEBUG("Update layer {} ", i);
        bool layer_connected = false;
        update_layer(i, &(layers[i]), &(layers[i + 1]), euclidean_distances[i], config, &layer_connected);
        if (!layer_connected)
        {
            SPDLOG_DEBUG("Traj unmatched as point {} and {} not connected", i, i + 1);
            if (all_connected != nullptr)
            {
                *all_connected = false;
            }
            return i;
            // tg->print_optimal_info();
        }
    }
    SPDLOG_DEBUG("Update transition graph done");
    if (all_connected != nullptr)
    {
        *all_connected = true;
    }
    return N - 1;
}

void FastMapMatch::update_layer(int level, TGLayer *la_ptr, TGLayer *lb_ptr, double euclidean_distance, const FastMapMatchConfig &config, bool *connected)
{
    TGLayer &lb = *lb_ptr;
    bool layer_connected = false;
    for (auto iter_a = la_ptr->begin(); iter_a != la_ptr->end(); ++iter_a)
    {
        NodeIndex source = iter_a->c->index;
        for (auto iter_b = lb_ptr->begin(); iter_b != lb_ptr->end(); ++iter_b)
        {
            // Calculate transition probability based on mode
            double tp;
            double path_cost; // Cost (distance or time) of path
            if (config.transition_mode == TransitionMode::FASTEST)
            {
                path_cost = get_time(iter_a->c, iter_b->c, config.reverse_tolerance);
                tp = TransitionGraph::get_fastest_transition_probability(path_cost, euclidean_distance, config.reference_speed.value());
            }
            else
            {
                path_cost = get_distance(iter_a->c, iter_b->c, config.reverse_tolerance);
                tp = TransitionGraph::get_shortest_transition_probability(path_cost, euclidean_distance);
            }

            // ---- Meili-style transition controls (opt-in; 0 = off => unchanged) ----
            // (1) Plausibility gate: reject transitions whose routed distance is an implausible
            //     multiple of the straight-line move (Meili max_route_distance_factor). Kills
            //     around-the-block detours and sparse-gap fills. Only meaningful in SHORTEST mode
            //     where path_cost is distance; in FASTEST mode path_cost is time and the
            //     comparison against euclidean_distance would be dimensionally wrong.
            if (config.transition_mode == TransitionMode::SHORTEST &&
                config.max_route_distance_factor > 0.0 && euclidean_distance > 1e-9 &&
                path_cost > config.max_route_distance_factor * euclidean_distance)
            {
                continue;
            }
            // (2) Turn penalty: discourage sharp turns / U-turns onto a different edge
            //     (Meili turn_penalty_factor). A ~180 deg flip onto the opposing carriageway
            //     is crushed -> fixes divided-road flapping. Penalises the a->b transition;
            //     b remains reachable via other predecessors in layer a.
            if (config.turn_penalty_factor > 0.0 && iter_a->c->edge->id != iter_b->c->edge->id)
            {
                const auto &ga = iter_a->c->edge->geom;
                const auto &gb = iter_b->c->edge->geom;
                int na = ga.get_num_points(), nb = gb.get_num_points();
                if (na >= 2 && nb >= 2)
                {
                    double ba = std::atan2(ga.get_y(na - 1) - ga.get_y(na - 2),
                                           ga.get_x(na - 1) - ga.get_x(na - 2));
                    double bb = std::atan2(gb.get_y(1) - gb.get_y(0),
                                           gb.get_x(1) - gb.get_x(0));
                    double turn = std::fabs((ba - bb) * 180.0 / M_PI);
                    if (turn > 180.0) turn = 360.0 - turn;
                    tp *= std::exp(-config.turn_penalty_factor * (turn / 180.0));
                }
            }

            double temp = iter_a->cumu_prob + log(tp) + log(iter_b->ep);
            SPDLOG_TRACE("L {} f {} t {} cost {} dist {} tp {} ep {} fcp {} tcp {}",
                         level, iter_a->c->edge->id, iter_b->c->edge->id,
                         path_cost, euclidean_distance, tp, iter_b->ep, iter_a->cumu_prob,
                         temp);
            if (temp >= iter_b->cumu_prob)
            {
                if (temp > -std::numeric_limits<double>::infinity())
                {
                    layer_connected = true;
                }
                iter_b->cumu_prob = temp;
                iter_b->prev = &(*iter_a);
                iter_b->tp = tp;
                iter_b->shortest_path_distance = path_cost; // Note: This stores cost (distance or time)
            }
        }
    }
    if (connected != nullptr)
    {
        *connected = layer_connected;
    }
}

PySplitMatchResult FastMapMatch::pymatch_trajectory(const CORE::Trajectory &trajectory,
                                                    int max_candidates,
                                                    double candidate_search_radius,
                                                    double gps_error,
                                                    double reverse_tolerance,
                                                    std::optional<double> reference_speed,
                                                    double max_route_distance_factor,
                                                    double turn_penalty_factor)
{
    // Algorithm: Automatic trajectory splitting with candidate reuse
    // 1. Do candidate search once for all points (performance optimization)
    // 2. Use a queue to process ranges [start_idx, end_idx]
    // 3. For each range:
    //    - If points have no candidates: split into continuous segments and queue them
    //    - If disconnected layers: split at disconnection point and queue both halves
    //    - If successful: add to results
    // 4. Only successful matches are returned in subtrajectories
    //    Failed points/ranges are simply excluded (not returned as failed sub-trajectories)

    FastMapMatchConfig config(
        max_candidates,
        candidate_search_radius,
        gps_error,
        reverse_tolerance,
        mode_,
        reference_speed,
        max_route_distance_factor,
        turn_penalty_factor);
    PySplitMatchResult output;
    int N = trajectory.geom.get_num_points();

    SPDLOG_DEBUG("Split matching trajectory with {} points", N);

    // Do candidate search once for all points
    TrajectoryCandidates tc = network_.search_tr_cs_knn(trajectory.geom, config.max_candidates, config.candidate_search_radius);
    SPDLOG_DEBUG("Trajectory candidates found for all points");

    // Queue of ranges to process [start_idx, end_idx]
    std::vector<std::pair<int, int>> ranges_to_process;
    ranges_to_process.push_back({0, N - 1});

    while (!ranges_to_process.empty())
    {
        auto range = ranges_to_process.back();
        ranges_to_process.pop_back();
        int start_idx = range.first;
        int end_idx = range.second;

        SPDLOG_DEBUG("Processing range [{}, {}]", start_idx, end_idx);

        // Single point - can't match, just add as failed
        if (start_idx >= end_idx)
        {
            if (start_idx > end_idx)
            {
                // This should not happen
                throw std::runtime_error("FastMapMatch::pymatch_trajectory: Invalid range with start_idx > end_idx");
            }
            SPDLOG_DEBUG("Single point at {}, skipping", start_idx);
            continue;
        }

        // Check for points with no candidates in this range
        std::vector<int> local_unmatched;
        for (int i = start_idx; i <= end_idx; ++i)
        {
            if (tc[i].empty())
            {
                local_unmatched.push_back(i);
            }
        }

        if (!local_unmatched.empty())
        {
            // Split around unmatched points - create continuous segments
            std::vector<int> keep_indices;
            for (int i = start_idx; i <= end_idx; ++i)
            {
                if (tc[i].empty() == false)
                {
                    keep_indices.push_back(i);
                }
            }

            // Build continuous segments from keep_indices
            std::vector<std::pair<int, int>> segments;
            if (!keep_indices.empty())
            {
                int seg_start = keep_indices[0];
                int seg_end = keep_indices[0];
                for (size_t i = 1; i < keep_indices.size(); ++i)
                {
                    if (keep_indices[i] == seg_end + 1)
                    {
                        seg_end = keep_indices[i];
                    }
                    else
                    {
                        if (seg_end > seg_start)
                        {
                            segments.push_back({seg_start, seg_end});
                        }
                        seg_start = keep_indices[i];
                        seg_end = keep_indices[i];
                    }
                }
                if (seg_end > seg_start)
                {
                    segments.push_back({seg_start, seg_end});
                }
            }

            SPDLOG_DEBUG("Found {} unmatched candidates, splitting into {} segments",
                         local_unmatched.size(), segments.size());

            // Add segments to process queue (in reverse order so they process in correct order)
            for (auto it = segments.rbegin(); it != segments.rend(); ++it)
            {
                ranges_to_process.push_back(*it);
            }
            continue;
        }

        // All points have candidates - try to match
        TrajectoryCandidates sub_tc(tc.begin() + start_idx, tc.begin() + end_idx + 1);
        TransitionGraph tg(sub_tc, config.gps_error);

        // Create sub-geometry for distance calculation
        CORE::LineString sub_geom;
        for (int i = start_idx; i <= end_idx; ++i)
        {
            sub_geom.add_point(trajectory.geom.get_x(i), trajectory.geom.get_y(i));
        }
        std::vector<double> euclidean_distances = ALGORITHM::calculate_linestring_euclidean_distances(sub_geom);

        // Update transition graph
        std::vector<TGLayer> &layers = tg.get_layers();
        bool fully_connected = true;
        int last_connected_local = -1;

        for (int i = 0; i < layers.size() - 1; ++i)
        {
            bool layer_connected = false;
            update_layer(i, &(layers[i]), &(layers[i + 1]), euclidean_distances[i], config, &layer_connected);

            if (!layer_connected)
            {
                SPDLOG_DEBUG("Disconnected at local points {} and {}", i, i + 1);
                last_connected_local = i;
                fully_connected = false;
                break;
            }
        }

        if (!fully_connected && last_connected_local >= 0)
        {
            // DISCONNECTED_LAYERS - split at the disconnection
            int split_point = start_idx + last_connected_local;

            SPDLOG_DEBUG("Disconnected layers, splitting at trajectory point {}", split_point + 1);

            // Add the two halves to the queue (in reverse order)
            if (split_point + 1 <= end_idx)
            {
                ranges_to_process.push_back({split_point + 1, end_idx});
            }
            if (start_idx <= split_point)
            {
                ranges_to_process.push_back({start_idx, split_point});
            }
            continue;
        }

        if (!fully_connected)
        {
            // Failed to match even the first two points - shouldn't happen if we checked candidates
            SPDLOG_WARN("Failed to match range [{}, {}] even though all points have candidates", start_idx, end_idx);
            continue;
        }

        // Successfully matched this range!
        TGOpath tg_opath = tg.backtrack();
        MatchedCandidatePath matched_path(tg_opath.size());
        std::transform(tg_opath.begin(), tg_opath.end(), matched_path.begin(),
                       [](const TGNode *a)
                       { return MatchedCandidate{*(a->c), a->ep, a->tp, a->shortest_path_distance}; });

        std::vector<int> indices;
        const std::vector<Edge> &edges = network_.get_edges();
        CompletePath complete_path = ubodt_->construct_complete_path(tg_opath, edges, &indices, config.reverse_tolerance);

        // Build PyMatchSegments
        PySubTrajectory success_sub;
        success_sub.start_index = start_idx;
        success_sub.end_index = end_idx;
        success_sub.error_code = MatchErrorCode::SUCCESS;
        success_sub.segments = build_py_segments(matched_path, complete_path, indices, trajectory, start_idx, end_idx);
        output.subtrajectories.push_back(success_sub);

        SPDLOG_DEBUG("Successfully matched range [{}, {}]", start_idx, end_idx);
    }

    // Sort the trajectory segments by start index - due to the FIFO there's a chance they're out of order.
    std::sort(output.subtrajectories.begin(), output.subtrajectories.end(),
              [](const PySubTrajectory &a, const PySubTrajectory &b)
              {
                  return a.start_index < b.start_index;
              });

    SPDLOG_DEBUG("Split matching complete: {} sub-trajectories", output.subtrajectories.size());
    return output;
}

std::vector<PySplitMatchResult> FastMapMatch::pymatch_many(const std::vector<CORE::Trajectory> &trajectories,
                                                           int max_candidates,
                                                           double candidate_search_radius,
                                                           double gps_error,
                                                           double reverse_tolerance,
                                                           std::optional<double> reference_speed,
                                                           double max_route_distance_factor,
                                                           double turn_penalty_factor,
                                                           std::optional<int> workers)
{
    if (trajectories.empty())
    {
        return {};
    }

    unsigned int worker_count = 0;
    if (workers.has_value())
    {
        if (workers.value() <= 0)
        {
            throw std::invalid_argument("FastMapMatch::match_many: workers must be positive");
        }
        worker_count = static_cast<unsigned int>(workers.value());
    }
    else
    {
        worker_count = std::thread::hardware_concurrency();
        if (worker_count == 0)
        {
            worker_count = 1;
        }
    }
    worker_count = std::min<unsigned int>(worker_count, static_cast<unsigned int>(trajectories.size()));

    std::vector<PySplitMatchResult> results(trajectories.size());
    std::atomic<std::size_t> next_index{0};
    std::exception_ptr first_error = nullptr;
    std::mutex error_mutex;

    auto run_worker = [&]()
    {
        while (true)
        {
            std::size_t idx = next_index.fetch_add(1, std::memory_order_relaxed);
            if (idx >= trajectories.size())
            {
                break;
            }
            try
            {
                results[idx] = pymatch_trajectory(
                    trajectories[idx],
                    max_candidates,
                    candidate_search_radius,
                    gps_error,
                    reverse_tolerance,
                    reference_speed,
                    max_route_distance_factor,
                    turn_penalty_factor);
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (first_error == nullptr)
                {
                    first_error = std::current_exception();
                }
                break;
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(worker_count);
    for (unsigned int i = 0; i < worker_count; ++i)
    {
        threads.emplace_back(run_worker);
    }
    for (auto &thread : threads)
    {
        thread.join();
    }

    if (first_error != nullptr)
    {
        std::rethrow_exception(first_error);
    }
    return results;
}

namespace
{
    // Fixed-precision join helpers matching the Python ETL formatting exactly:
    //   cpath/opath -> str(int)      length -> "%.6f"      duration -> "%.3f"
    std::string join_ints(const std::vector<int> &xs)
    {
        std::string out;
        for (std::size_t i = 0; i < xs.size(); ++i)
        {
            if (i)
                out.push_back(',');
            out += std::to_string(xs[i]);
        }
        return out;
    }

    std::string join_fixed(const std::vector<double> &xs, int precision)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision);
        for (std::size_t i = 0; i < xs.size(); ++i)
        {
            if (i)
                oss << ',';
            oss << xs[i];
        }
        return oss.str();
    }

    // Native port of the ETL's _rows_from_result (minus snap_flag, which the caller
    // derives from the returned scalars). Folds the deep match tree into the four
    // output strings + status/counts in a single pass.
    PyMatchRows materialize_rows(const PySplitMatchResult &result,
                                 const std::vector<int> &orig_idx,
                                 int orig_len)
    {
        std::vector<int> cpath;       // deduplicated edge IDs
        std::vector<double> lengths;  // per-edge length (degrees, cumulative)
        std::vector<double> durations;// per-edge duration (seconds)
        std::vector<int> opath(orig_len < 0 ? 0 : orig_len, -1);

        int n_success = 0;
        const int n_total = static_cast<int>(result.subtrajectories.size());
        int n_reversed = 0;
        double max_snap_dist = 0.0;
        bool any_snap = false;
        const int orig_idx_n = static_cast<int>(orig_idx.size());

        for (const auto &sub : result.subtrajectories)
        {
            if (sub.error_code != MatchErrorCode::SUCCESS)
                continue;
            ++n_success;
            for (const auto &seg : sub.segments)
            {
                const int c0 = seg.p0.trajectory_index - 1;
                const int c1 = seg.p1.trajectory_index - 1;
                const int first_edge_id = seg.edges.empty() ? -1 : seg.edges.front().edge_id;
                const int last_edge_id = seg.edges.empty() ? -1 : seg.edges.back().edge_id;
                if (c0 >= 0 && c0 < orig_idx_n)
                    opath[orig_idx[c0]] = first_edge_id;
                if (c1 >= 0 && c1 < orig_idx_n)
                    opath[orig_idx[c1]] = last_edge_id;

                // snap distances (both endpoints) over successful segments
                max_snap_dist = any_snap ? std::max(max_snap_dist, seg.p0.perpendicular_distance_to_matched_geometry)
                                         : seg.p0.perpendicular_distance_to_matched_geometry;
                any_snap = true;
                max_snap_dist = std::max(max_snap_dist, seg.p1.perpendicular_distance_to_matched_geometry);

                for (const auto &e : seg.edges)
                {
                    double edge_len = 0.0;
                    double edge_dur = 0.0;
                    if (!e.points.empty())
                    {
                        edge_len = e.points.back().cumulative_distance - e.points.front().cumulative_distance;
                        const double dt = e.points.back().t - e.points.front().t;
                        edge_dur = std::isfinite(dt) ? dt : 0.0;
                    }
                    if (e.reversed)
                        ++n_reversed;

                    if (!cpath.empty() && cpath.back() == e.edge_id)
                    {
                        lengths.back() += edge_len;
                        durations.back() += edge_dur;
                    }
                    else
                    {
                        cpath.push_back(e.edge_id);
                        lengths.push_back(edge_len);
                        durations.push_back(edge_dur);
                    }
                }
            }
        }

        PyMatchRows rows;
        rows.cpath = join_ints(cpath);
        rows.opath = join_ints(opath);
        rows.length = join_fixed(lengths, 6);
        rows.duration = join_fixed(durations, 3);
        rows.n_sub = n_total;
        rows.n_reversed = n_reversed;
        rows.max_snap_dist_deg = any_snap ? max_snap_dist : 0.0;
        rows.match_status = (n_success == 0) ? "failed"
                            : (n_success < n_total) ? "partial"
                                                    : "full";
        return rows;
    }
} // namespace

std::vector<PyMatchRows> FastMapMatch::pymatch_many_rows(const std::vector<CORE::Trajectory> &trajectories,
                                                         const std::vector<std::vector<int>> &orig_idx_list,
                                                         const std::vector<int> &orig_len_list,
                                                         int max_candidates,
                                                         double candidate_search_radius,
                                                         double gps_error,
                                                         double reverse_tolerance,
                                                         std::optional<double> reference_speed,
                                                         double max_route_distance_factor,
                                                         double turn_penalty_factor,
                                                         std::optional<int> workers)
{
    if (trajectories.empty())
    {
        return {};
    }
    if (orig_idx_list.size() != trajectories.size() || orig_len_list.size() != trajectories.size())
    {
        throw std::invalid_argument(
            "FastMapMatch::match_many_rows: orig_idx_list and orig_len_list must match trajectories length");
    }

    unsigned int worker_count = 0;
    if (workers.has_value())
    {
        if (workers.value() <= 0)
        {
            throw std::invalid_argument("FastMapMatch::match_many_rows: workers must be positive");
        }
        worker_count = static_cast<unsigned int>(workers.value());
    }
    else
    {
        worker_count = std::thread::hardware_concurrency();
        if (worker_count == 0)
        {
            worker_count = 1;
        }
    }
    worker_count = std::min<unsigned int>(worker_count, static_cast<unsigned int>(trajectories.size()));

    std::vector<PyMatchRows> results(trajectories.size());
    std::atomic<std::size_t> next_index{0};
    std::exception_ptr first_error = nullptr;
    std::mutex error_mutex;

    auto run_worker = [&]()
    {
        while (true)
        {
            std::size_t idx = next_index.fetch_add(1, std::memory_order_relaxed);
            if (idx >= trajectories.size())
            {
                break;
            }
            try
            {
                PySplitMatchResult match = pymatch_trajectory(
                    trajectories[idx],
                    max_candidates,
                    candidate_search_radius,
                    gps_error,
                    reverse_tolerance,
                    reference_speed,
                    max_route_distance_factor,
                    turn_penalty_factor);
                results[idx] = materialize_rows(match, orig_idx_list[idx], orig_len_list[idx]);
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (first_error == nullptr)
                {
                    first_error = std::current_exception();
                }
                break;
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(worker_count);
    for (unsigned int i = 0; i < worker_count; ++i)
    {
        threads.emplace_back(run_worker);
    }
    for (auto &thread : threads)
    {
        thread.join();
    }

    if (first_error != nullptr)
    {
        std::rethrow_exception(first_error);
    }
    return results;
}

std::vector<PyMatchSegment> FastMapMatch::build_py_segments(const MatchedCandidatePath &matched_path,
                                                            const CompletePath &complete_path,
                                                            const std::vector<int> &indices,
                                                            const CORE::Trajectory &trajectory,
                                                            int start_idx,
                                                            int end_idx)
{
    std::vector<PyMatchSegment> segments;
    double cumulative_distance = 0;

    for (int i = 1; i < matched_path.size(); ++i)
    {
        const MatchedCandidate &mc0 = matched_path[i - 1];
        const MatchedCandidate &mc1 = matched_path[i];

        const int start_edge_index = indices[i - 1];
        const int end_edge_index = indices[i];

        if (start_edge_index < 0 || start_edge_index >= complete_path.size() ||
            end_edge_index < 0 || end_edge_index >= complete_path.size())
        {
            SPDLOG_WARN("Edge index out of bounds");
            continue;
        }

        int traj_idx_0 = start_idx + i - 1;
        int traj_idx_1 = start_idx + i;

        const double t0 = trajectory.has_timestamps() ? trajectory.timestamps[traj_idx_0] : -1.0;
        const PyMatchCandidate start_candidate = {
            boost::geometry::get<0>(mc0.c.point),
            boost::geometry::get<1>(mc0.c.point),
            t0,
            mc0.c.dist,
            mc0.c.offset,
            traj_idx_0 };

        const double t1 = trajectory.has_timestamps() ? trajectory.timestamps[traj_idx_1] : -1.0;
        const PyMatchCandidate end_candidate = {
            boost::geometry::get<0>(mc1.c.point),
            boost::geometry::get<1>(mc1.c.point),
            t1,
            mc1.c.dist,
            mc1.c.offset,
            traj_idx_1};

        PyMatchSegment segment = {start_candidate, end_candidate, {}};

        if (start_edge_index == end_edge_index)
        {
            // Single edge
            EdgeID edge_id = complete_path[start_edge_index];
            const Edge &e0 = network_.get_edge(edge_id);
            bool is_reversed = (mc0.c.offset > mc1.c.offset);
            FASTMM::CORE::LineString line = ALGORITHM::cutoffseg_unique(e0.geom, mc0.c.offset, mc1.c.offset);
            std::vector<double> distances = ALGORITHM::calculate_linestring_euclidean_distances(line);
            std::vector<PyMatchPoint> points;
            double start_distance = cumulative_distance;

            for (int j = 0; j < line.get_num_points(); ++j)
            {
                double d = (j == 0) ? 0.0 : distances[j - 1];
                if (j > 0)
                {
                    cumulative_distance += d;
                }
                points.push_back({line.get_x(j), line.get_y(j), d, -1.0, e0.speed, cumulative_distance - start_distance, cumulative_distance});
            }
            segment.edges.push_back({edge_id, points, is_reversed});
        }
        else
        {
            // Multiple edges - first edge
            EdgeID edge_id = complete_path[start_edge_index];
            Edge e = network_.get_edge(edge_id);
            FASTMM::CORE::LineString line = ALGORITHM::cutoffseg_unique(e.geom, mc0.c.offset, e.length);
            std::vector<double> distances = ALGORITHM::calculate_linestring_euclidean_distances(line);
            std::vector<PyMatchPoint> points;
            double start_distance = cumulative_distance;
            double d;
            for (int j = 0; j < line.get_num_points(); ++j)
            {
                d = (j == 0) ? 0.0 : distances[j - 1];
                if (j > 0)
                {
                    cumulative_distance += d;
                }
                points.push_back({line.get_x(j), line.get_y(j), d, -1.0, e.speed, cumulative_distance - start_distance, cumulative_distance});
            }
            segment.edges.push_back({edge_id, points, false});

            // Middle edges
            for (int j = start_edge_index + 1; j < end_edge_index; ++j)
            {
                edge_id = complete_path[j];
                e = network_.get_edge(edge_id);
                line = e.geom;
                distances = ALGORITHM::calculate_linestring_euclidean_distances(line);
                points.clear();
                start_distance = cumulative_distance;

                for (int k = 0; k < line.get_num_points(); ++k)
                {
                    d = (k == 0) ? 0.0 : distances[k - 1];
                    if (k > 0)
                    {
                        cumulative_distance += d;
                    }
                    points.push_back({line.get_x(k), line.get_y(k), d, -1.0, e.speed, cumulative_distance - start_distance, cumulative_distance});
                }
                segment.edges.push_back({edge_id, points, false});
            }

            // Last edge
            edge_id = complete_path[end_edge_index];
            e = network_.get_edge(edge_id);
            line = ALGORITHM::cutoffseg_unique(e.geom, 0, mc1.c.offset);
            distances = ALGORITHM::calculate_linestring_euclidean_distances(line);
            points.clear();
            start_distance = cumulative_distance;

            for (int j = 0; j < line.get_num_points(); ++j)
            {
                d = (j == 0) ? 0.0 : distances[j - 1];
                if (j > 0)
                {
                    cumulative_distance += d;
                }
                points.push_back({line.get_x(j), line.get_y(j), d, -1.0, e.speed, cumulative_distance - start_distance, cumulative_distance});
            }
            segment.edges.push_back({edge_id, points, false});
        }

        // OK, great, we can now apportion the time appropriately along the segment:
        if (trajectory.has_timestamps())
        {
            if (network_.all_edges_have_speed())
            {
                // We've got speed, so apportion the time t0 -> t1 according to expected travel time along the segment
                double total_expected_time = 0;
                for (const auto &edge : segment.edges)
                {
                    for (const auto &point : edge.points)
                    {
                        total_expected_time += point.d / point.speed;
                    }
                }

                // OK, cool, now go updating times:
                double cumulative_expected_time = 0;
                for (int edge_idx = 0; edge_idx < segment.edges.size(); ++edge_idx)
                {
                    auto &edge = segment.edges[edge_idx];
                    for (int point_idx = 0; point_idx < edge.points.size(); ++point_idx)
                    {
                        auto &point = edge.points[point_idx];
                        if (edge_idx == 0 && point_idx == 0)
                        {
                            // First point - use original timestamp
                            point.t = t0;
                        }
                        else
                        {
                            cumulative_expected_time += point.d / point.speed;
                            if (total_expected_time > 0)
                            {
                                point.t = t0 + (cumulative_expected_time / total_expected_time) * (t1 - t0);
                            }
                            else
                            {
                                point.t = t0;
                            }
                        }
                    }
                }
            }
            else
            {
                // OK, we don't have speeds, so just linearly interpolate time along the segment according to distance

                // Get total distance of segment - subtract last point from last edge from first point of first edge
                double total_segment_distance = 0;
                for (const auto &edge : segment.edges)
                {
                    for (const auto &point : edge.points)
                    {
                        total_segment_distance += point.d;
                    }
                }
                double cumulative_segment_distance = 0;
                for (int edge_idx = 0; edge_idx < segment.edges.size(); ++edge_idx)
                {
                    auto &edge = segment.edges[edge_idx];
                    for (int point_idx = 0; point_idx < edge.points.size(); ++point_idx)
                    {
                        auto &point = edge.points[point_idx];
                        if (edge_idx == 0 && point_idx == 0)
                        {
                            // First point - use original timestamp
                            point.t = t0;
                        }
                        else
                        {
                            cumulative_segment_distance += point.d;
                            if (total_segment_distance > 0)
                            {
                                point.t = t0 + (cumulative_segment_distance / total_segment_distance) * (t1 - t0);
                            }
                            else
                            {
                                point.t = t0;
                            }
                        }
                    }
                }
            }
        }
        segments.push_back(segment);
    }

    return segments;
}
