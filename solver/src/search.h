#pragma once
#include "operators.h"

// ─── OperatorSelector ─────────────────────────────────────────────────────────
// Encapsulates warmup + adaptive UCB-style sampling + exponential decay.
//
// Calling contract:
//   int op = sel.pick(iter);
//   ... apply op ...
//   sel.record(op, improved);
//   sel.maybe_decay(iter);
class OperatorSelector {
public:
    static constexpr int WARMUP_ITERS = 150;
    static constexpr int DECAY_PERIOD = 300;

    explicit OperatorSelector(const vector<int>& active_ops);

    int pick(int iter);
    void record(int op, bool improved);
    void maybe_decay(int iter);

private:
    vector<int> active_ops_;
    vector<int> unique_ops_;
    array<int,       NUM_OPERATORS> prior_count_{};
    array<long long, NUM_OPERATORS> roll_tried_{};
    array<long long, NUM_OPERATORS> roll_improved_{};
};

// ─── Search functions ─────────────────────────────────────────────────────────

vector<PlacedBay> run_search(
    vector<PlacedBay> sol,
    const OperatorContext& ctx,
    OperatorStats& stats,
    bool axis_aligned,
    Clock::time_point deadline,
    bool use_sa,
    double t0_frac = 0.05
);

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
);

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
    double t0_frac = 0.05
);
