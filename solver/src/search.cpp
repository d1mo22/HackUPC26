#include "search.h"

// ─── OperatorSelector ─────────────────────────────────────────────────────────

OperatorSelector::OperatorSelector(const vector<int>& active_ops) : active_ops_(active_ops) {
    for (int o : active_ops_) {
        prior_count_[o]++;
        if (find(unique_ops_.begin(), unique_ops_.end(), o) == unique_ops_.end()) {
            unique_ops_.push_back(o);
        }
    }
}

int OperatorSelector::pick(int iter) {
    if (iter < WARMUP_ITERS) {
        return active_ops_[rng() % active_ops_.size()];
    }
    // Adaptive: weight = prior * (improved+1)/(tried+4)
    double total = 0.0;
    double w[NUM_OPERATORS] = {};
    for (int o : unique_ops_) {
        double r = (double)(roll_improved_[o] + 1) / (double)(roll_tried_[o] + 4);
        w[o] = (double)prior_count_[o] * r;
        total += w[o];
    }
    double pick = uniform_real_distribution<double>(0.0, total)(rng);
    double acc = 0.0;
    int op = unique_ops_.back();
    for (int o : unique_ops_) {
        acc += w[o];
        if (pick <= acc) { op = o; break; }
    }
    return op;
}

void OperatorSelector::record(int op, bool improved) {
    roll_tried_[op]++;
    if (improved) roll_improved_[op]++;
}

void OperatorSelector::maybe_decay(int iter) {
    if (iter % DECAY_PERIOD == 0) {
        for (int o : unique_ops_) {
            roll_tried_[o]   /= 2;
            roll_improved_[o] /= 2;
        }
    }
}

// ─── run_search ───────────────────────────────────────────────────────────────
// Single loop that drives both hill-climbing and simulated annealing.
// Unifies the ~95%-identical hill() and hill_sa() bodies.
//
// use_sa=false → strict improvement (original hill()).
// use_sa=true  → SA acceptance with linear cooling (original hill_sa()).
//
// The RNG call order is preserved verbatim from both originals:
//   1. sel.pick() — may draw from rng for adaptive selection
//   2. OPERATORS[op].fn() — operators consume rng internally
//   3. SA only: uniform_real_distribution draw for Metropolis criterion
vector<PlacedBay> run_search(
    vector<PlacedBay> sol,
    const OperatorContext& ctx,
    OperatorStats& stats,
    bool axis_aligned,
    Clock::time_point deadline,
    bool use_sa,
    double t0_frac
) {
    double wh_area = ctx.wh_area;

    // SA state
    auto sa_start = Clock::now();
    double total_seconds = chrono::duration<double>(deadline - sa_start).count();
    if (total_seconds <= 0.0) total_seconds = 1.0;

    vector<PlacedBay> current = sol;
    double current_q = quality(current, wh_area);

    vector<PlacedBay> best = current;
    double best_q = current_q;

    double T0 = max(20.0, t0_frac * current_q);  // ignored when use_sa=false

    OperatorSelector sel(build_active_ops(axis_aligned));

    int iter = 0;
    while (Clock::now() < deadline && iter < ITERATIONS) {
        // Hill: mutate from best. SA: mutate from current chain.
        vector<PlacedBay> candidate = use_sa ? current : best;

        int op = sel.pick(iter);
        stats.tried[op]++;

        OPERATORS[op].fn(candidate, ctx);

        double q = quality(candidate, wh_area);

        bool accept;
        if (!use_sa) {
            accept = (q < best_q);
        } else if (q < current_q) {
            accept = true;
        } else {
            // Metropolis criterion (only in SA mode; preserves exact RNG call from hill_sa).
            double elapsed = chrono::duration<double>(Clock::now() - sa_start).count();
            double progress = min(1.0, elapsed / total_seconds);
            double T = max(1e-6, T0 * (1.0 - progress));
            double dQ = q - current_q;
            double prob = exp(-dQ / T);
            double u = uniform_real_distribution<double>(0.0, 1.0)(rng);
            accept = (u < prob);
        }

        bool improved_best = false;
        if (accept) {
            current   = candidate;
            current_q = q;
            if (q < best_q) {
                best      = candidate;
                best_q    = q;
                improved_best = true;
            }
        } else if (!use_sa) {
            // Hill mode: current always tracks best (no SA chain).
            current   = best;
            current_q = best_q;
        }

        if (improved_best || (!use_sa && accept)) {
            stats.improved[op]++;
        }
        sel.record(op, improved_best || (!use_sa && accept));

        iter++;
        sel.maybe_decay(iter);
    }

    return best;
}

// Thin wrappers preserve the original call-site signatures.
vector<PlacedBay> hill(
    vector<PlacedBay> sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode,
    OperatorStats& stats,
    bool axis_aligned,
    Clock::time_point deadline
) {
    OperatorContext ctx{types, warehouse, obstacles, ceiling, wh_area, mode};
    return run_search(sol, ctx, stats, axis_aligned, deadline, /*use_sa=*/false);
}

vector<PlacedBay> hill_sa(
    vector<PlacedBay> sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode,
    OperatorStats& stats,
    bool axis_aligned,
    Clock::time_point deadline,
    double t0_frac
) {
    OperatorContext ctx{types, warehouse, obstacles, ceiling, wh_area, mode};
    return run_search(sol, ctx, stats, axis_aligned, deadline, /*use_sa=*/true, t0_frac);
}
