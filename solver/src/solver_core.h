#pragma once
#include "scoring.h"
#include "case_io.h"

// ---- candidate points / make_candidate ----
vector<Point> candidate_points(
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<PlacedBay>& sol
);

PlacedBay make_candidate(const BayType& t, double x, double y, int angle);

// ---- operators ----
bool add_bay(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx,
    const vector<int>& angles_source = ANGLES
);

void fill_aggressive(vector<PlacedBay>& sol, const OperatorContext& ctx);
void remove_k_and_refill(vector<PlacedBay>& sol, const OperatorContext& ctx);
void upgrade_bay(vector<PlacedBay>& sol, const OperatorContext& ctx);
void replace_bay(vector<PlacedBay>& sol, const OperatorContext& ctx);
void split_bay(vector<PlacedBay>& sol, const OperatorContext& ctx);

Point angle_width_axis(int angle);
Point angle_depth_axis(int angle);

bool valid_placed_bay_at(
    const PlacedBay& bay,
    const vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    int ignore
);

bool compact_diagonal_bays_on_axes(
    vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling
);

bool add_shared_gap_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
);

void shared_gap_refill(vector<PlacedBay>& sol, const OperatorContext& ctx);
void rotate_compact_and_add(vector<PlacedBay>& sol, const OperatorContext& ctx);
void add_45_degree_bay(vector<PlacedBay>& sol, const OperatorContext& ctx);
void position_perturb_and_add(vector<PlacedBay>& sol, const OperatorContext& ctx);

// ---- operator table ----
struct OperatorEntry {
    const char* name;
    void (*fn)(vector<PlacedBay>&, const OperatorContext&);
    int weight_axis;
    int weight_angled;
};

extern const OperatorEntry OPERATORS[NUM_OPERATORS];

vector<int> build_active_ops(bool axis_aligned);

// ---- OperatorSelector ----
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

// ---- search ----
bool is_valid_solution(
    const vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling
);

bool warehouse_is_axis_aligned(const vector<Point>& warehouse);

void shelf_pack_into(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode,
    int orientation,
    double off_x,
    double off_y
);

vector<PlacedBay> build_initial(
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode,
    bool axis_aligned,
    int strategy
);

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

void print_operator_stats(const string& case_dir, const OperatorStats& stats);

// ---- top-level ----
double seconds_since(Clock::time_point start);
void solve_case(const string& case_dir);
