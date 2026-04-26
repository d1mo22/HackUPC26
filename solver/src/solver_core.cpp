#include "solver_core.h"

// ================= OPERATORS =================

vector<Point> candidate_points(
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<PlacedBay>& sol
) {
    vector<Point> pts;

    for (auto p : warehouse) pts.push_back(p);

    for (auto& o : obstacles) {
        pts.push_back({o.x, o.y});
        pts.push_back({o.x + o.w, o.y});
        pts.push_back({o.x, o.y + o.d});
        pts.push_back({o.x + o.w, o.y + o.d});
        // edge midpoints — tangent placement against obstacle sides
        pts.push_back({o.x + o.w / 2.0, o.y});
        pts.push_back({o.x + o.w / 2.0, o.y + o.d});
        pts.push_back({o.x, o.y + o.d / 2.0});
        pts.push_back({o.x + o.w, o.y + o.d / 2.0});
    }

    for (auto& p : sol) {
        auto& bp = bay_poly(p);
        auto& gp = gap_poly(p);

        for (auto q : bp) pts.push_back({round(q.x), round(q.y)});
        for (auto q : gp) pts.push_back({round(q.x), round(q.y)});

        // edge midpoints of the bay rectangle — tangent slot finder
        if ((int)bp.size() == 4) {
            for (int i = 0; i < 4; i++) {
                Point a = bp[i];
                Point b = bp[(i + 1) % 4];
                pts.push_back({round((a.x + b.x) / 2.0), round((a.y + b.y) / 2.0)});
            }
        }
    }

    shuffle(pts.begin(), pts.end(), rng);

    if ((int)pts.size() > 500) pts.resize(500);

    return pts;
}

PlacedBay make_candidate(const BayType& t, double x, double y, int angle) {
    PlacedBay p;
    p.id = t.id;
    p.x = x;
    p.y = y;
    p.w = t.w;
    p.d = t.d;
    p.h = t.h;
    p.gap = t.gap;
    p.angle = angle;
    p.price = t.price;
    p.loads = t.loads;
    refresh_cache(p);
    return p;
}

bool add_bay(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx,
    const vector<int>& angles_source
) {
    const auto& types    = ctx.types;
    const auto& warehouse = ctx.warehouse;
    const auto& obstacles = ctx.obstacles;
    const auto& ceiling   = ctx.ceiling;
    double wh_area = ctx.wh_area;
    int mode       = ctx.mode;
    auto pts = candidate_points(warehouse, obstacles, sol);

    vector<BayType> sorted_types = types;

    sort(sorted_types.begin(), sorted_types.end(), [&](const BayType& a, const BayType& b) {
        return bay_score_mode(a, mode, wh_area) > bay_score_mode(b, mode, wh_area);
    });

    PlacedBay best;
    bool found = false;
    double best_s = -1e100;

    int point_limit = min(MAX_POINTS_ADD, (int)pts.size());

    SpatialIndex idx;
    idx.build(sol);

    for (int pi = 0; pi < point_limit; pi++) {
        double x = pts[pi].x;
        double y = pts[pi].y;

        for (auto& t : sorted_types) {
            vector<int> angles = prioritized_angles(angles_source);

            int limit = min(ANGLE_SAMPLE, (int)angles.size());

            for (int ai = 0; ai < limit; ai++) {
                int angle = angles[ai];

                if (valid_candidate(
                        x,
                        y,
                        t.w,
                        t.d,
                        t.h,
                        t.gap,
                        angle,
                        sol,
                        warehouse,
                        obstacles,
                        ceiling,
                        -1,
                        &idx
                    )) {
                    double s = candidate_score_mode(t, x, y, angle, wh_area, mode);

                    if (s > best_s) {
                        best_s = s;
                        best = make_candidate(t, x, y, angle);
                        found = true;
                    }
                }
            }
        }
    }

    if (found) {
        sol.push_back(best);
        return true;
    }

    return false;
}

void fill_aggressive(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx
) {
    for (int i = 0; i < 5; i++) {
        if (!add_bay(sol, ctx)) {
            break;
        }
    }
}

void remove_k_and_refill(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx
) {
    double wh_area = ctx.wh_area;
    if (sol.empty()) return;

    vector<PlacedBay> backup = sol;

    int k = min(4, max(1, (int)sol.size() / 5));

    for (int i = 0; i < k && !sol.empty(); i++) {
        int idx = rng() % sol.size();
        sol.erase(sol.begin() + idx);
    }

    for (int i = 0; i < k + 3; i++) {
        add_bay(sol, ctx);
    }

    if (quality(sol, wh_area) > quality(backup, wh_area)) {
        sol = backup;
    }
}

void upgrade_bay(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx
) {
    const auto& types    = ctx.types;
    const auto& warehouse = ctx.warehouse;
    const auto& obstacles = ctx.obstacles;
    const auto& ceiling   = ctx.ceiling;
    double wh_area = ctx.wh_area;
    if (sol.empty()) return;

    int idx = rng() % sol.size();
    PlacedBay old = sol[idx];

    sol.erase(sol.begin() + idx);

    QSums cur = compute_sums(sol);
    double best_q = quality_with_added(cur, old.price, old.loads, (double)old.w * old.d, wh_area);
    PlacedBay best = old;
    bool found = false;

    SpatialIndex idx_grid;
    idx_grid.build(sol);

    for (auto& t : types) {
        vector<int> angles = prioritized_angles(ANGLES);

        int limit = min(ANGLE_SAMPLE, (int)angles.size());

        for (int i = 0; i < limit; i++) {
            int angle = angles[i];

            if (valid_candidate(
                    old.x,
                    old.y,
                    t.w,
                    t.d,
                    t.h,
                    t.gap,
                    angle,
                    sol,
                    warehouse,
                    obstacles,
                    ceiling,
                    -1,
                    &idx_grid
                )) {
                double q = quality_with_added(cur, t.price, t.loads, (double)t.w * t.d, wh_area);

                if (q < best_q) {
                    best_q = q;
                    best = make_candidate(t, old.x, old.y, angle);
                    found = true;
                }
            }
        }
    }

    sol.push_back(found ? best : old);
}

void replace_bay(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx
) {
    double wh_area = ctx.wh_area;
    if (sol.empty()) return;

    vector<PlacedBay> backup = sol;

    int idx = rng() % sol.size();
    sol.erase(sol.begin() + idx);

    bool ok = add_bay(sol, ctx);

    if (!ok || quality(sol, wh_area) > quality(backup, wh_area)) {
        sol = backup;
    }
}

void split_bay(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx
) {
    const auto& types    = ctx.types;
    const auto& warehouse = ctx.warehouse;
    const auto& obstacles = ctx.obstacles;
    const auto& ceiling   = ctx.ceiling;
    double wh_area = ctx.wh_area;
    if (sol.empty()) return;

    vector<PlacedBay> backup = sol;
    double backup_q = quality(backup, wh_area);

    vector<int> rotated;
    rotated.reserve(sol.size());
    for (int i = 0; i < (int)sol.size(); i++) {
        int a = sol[i].angle % 360;
        if (a < 0) a += 360;
        if (a != 0 && a != 90 && a != 180 && a != 270) {
            rotated.push_back(i);
        }
    }
    if (rotated.empty()) return;

    int idx = rotated[rng() % rotated.size()];
    const PlacedBay& victim = sol[idx];
    double v_bay_area = (double)victim.w * (double)victim.d;
    double v_bbox_area = (victim.bay_max_x - victim.bay_min_x) *
                         (victim.bay_max_y - victim.bay_min_y);

    if (v_bbox_area < v_bay_area * 1.15) return;

    vector<int> smaller;
    smaller.reserve(types.size());
    for (int ti = 0; ti < (int)types.size(); ti++) {
        double a = (double)types[ti].w * (double)types[ti].d;
        if (a < v_bay_area + EPS) smaller.push_back(ti);
    }
    if (smaller.empty()) return;

    double bx_min = victim.bay_min_x;
    double by_min = victim.bay_min_y;
    double bx_max = victim.bay_max_x;
    double by_max = victim.bay_max_y;

    sol.erase(sol.begin() + idx);

    static const int try_angles[4] = {0, 90, 180, 270};
    int placed = 0;
    const int MAX_ATTEMPTS = 12;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        double rx = uniform_real_distribution<double>(bx_min, bx_max)(rng);
        double ry = uniform_real_distribution<double>(by_min, by_max)(rng);

        int ti = smaller[rng() % smaller.size()];
        const BayType& t = types[ti];

        bool added = false;
        for (int ai = 0; ai < 4 && !added; ai++) {
            int angle = try_angles[ai];
            if (valid_candidate(rx, ry, t.w, t.d, t.h, t.gap, angle,
                                sol, warehouse, obstacles, ceiling)) {
                sol.push_back(make_candidate(t, rx, ry, angle));
                placed++;
                added = true;
            }
        }
    }

    if (placed < 2 || quality(sol, wh_area) >= backup_q) {
        sol = backup;
    }
}

Point angle_width_axis(int angle) {
    const Trig& trig = trig_for_angle(angle);
    return {trig.cos_v, trig.sin_v};
}

Point angle_depth_axis(int angle) {
    const Trig& trig = trig_for_angle(angle);
    return {-trig.sin_v, trig.cos_v};
}

bool valid_placed_bay_at(
    const PlacedBay& bay,
    const vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    int ignore
) {
    return valid_candidate(
        bay.x,
        bay.y,
        bay.w,
        bay.d,
        bay.h,
        bay.gap,
        bay.angle,
        sol,
        warehouse,
        obstacles,
        ceiling,
        ignore
    );
}

bool valid_slide_distance(
    const vector<PlacedBay>& sol,
    int idx,
    Point dir,
    double dist,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling
) {
    PlacedBay moved = sol[idx];
    moved.x += dir.x * dist;
    moved.y += dir.y * dist;

    return valid_placed_bay_at(moved, sol, warehouse, obstacles, ceiling, idx);
}

double max_touching_slide_distance(
    const vector<PlacedBay>& sol,
    int idx,
    Point dir,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling
) {
    double lo = 0.0;
    double hi = DIAGONAL_COMPACT_MIN_SHIFT;

    while (
        hi < DIAGONAL_COMPACT_MAX_SHIFT &&
        valid_slide_distance(sol, idx, dir, hi, warehouse, obstacles, ceiling)
    ) {
        lo = hi;
        hi *= 2.0;
    }

    if (hi >= DIAGONAL_COMPACT_MAX_SHIFT &&
        valid_slide_distance(sol, idx, dir, DIAGONAL_COMPACT_MAX_SHIFT, warehouse, obstacles, ceiling)) {
        return 0.0;
    }

    hi = min(hi, DIAGONAL_COMPACT_MAX_SHIFT);

    for (int it = 0; it < DIAGONAL_COMPACT_BINARY_ITERS; it++) {
        double mid = (lo + hi) / 2.0;

        if (valid_slide_distance(sol, idx, dir, mid, warehouse, obstacles, ceiling)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    if (lo < DIAGONAL_COMPACT_MIN_SHIFT) {
        return 0.0;
    }

    return lo;
}

bool compact_diagonal_bays_on_axes(
    vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling
) {
    if (sol.empty()) return false;

    bool changed = false;
    vector<int> order(sol.size());
    iota(order.begin(), order.end(), 0);
    shuffle(order.begin(), order.end(), rng);

    int processed = 0;

    for (int idx : order) {
        if (!is_diagonal_45_angle(sol[idx].angle)) continue;
        if (processed >= DIAGONAL_COMPACT_MAX_BAYS) break;
        processed++;

        Point u = angle_width_axis(sol[idx].angle);
        Point v = angle_depth_axis(sol[idx].angle);

        vector<Point> dirs = {
            u,
            {-u.x, -u.y},
            v,
            {-v.x, -v.y}
        };

        shuffle(dirs.begin(), dirs.end(), rng);

        double best_dist = 0.0;
        Point best_dir = {0, 0};

        for (Point dir : dirs) {
            double dist = max_touching_slide_distance(sol, idx, dir, warehouse, obstacles, ceiling);

            if (dist > best_dist) {
                best_dist = dist;
                best_dir = dir;
            }
        }

        if (best_dist > 0.0) {
            sol[idx].x += best_dir.x * best_dist;
            sol[idx].y += best_dir.y * best_dist;
            refresh_cache(sol[idx]);
            changed = true;
        }
    }

    return changed;
}

bool add_shared_gap_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
) {
    if (sol.empty()) return false;

    vector<int> anchors(sol.size());
    iota(anchors.begin(), anchors.end(), 0);
    shuffle(anchors.begin(), anchors.end(), rng);

    if ((int)anchors.size() > SHARED_GAP_MAX_ANCHORS) {
        anchors.resize(SHARED_GAP_MAX_ANCHORS);
    }

    vector<BayType> sorted_types = types;

    sort(sorted_types.begin(), sorted_types.end(), [&](const BayType& a, const BayType& b) {
        return bay_score_mode(a, mode, wh_area) > bay_score_mode(b, mode, wh_area);
    });

    PlacedBay best;
    bool found = false;
    double best_q = 1e100;

    QSums cur = compute_sums(sol);

    SpatialIndex idx_grid;
    idx_grid.build(sol);

    for (int idx : anchors) {
        PlacedBay anchor = sol[idx];

        if (anchor.gap <= 0) continue;

        Point u = angle_width_axis(anchor.angle);
        Point v = angle_depth_axis(anchor.angle);

        for (auto& t : sorted_types) {
            if (t.gap <= 0) continue;

            int angle = (anchor.angle + 180) % 360;
            double depth_offset = anchor.d + anchor.gap + t.d;

            vector<double> lateral_offsets = {
                (double)t.w,
                (double)anchor.w,
                ((double)anchor.w + t.w) / 2.0
            };

            shuffle(lateral_offsets.begin(), lateral_offsets.end(), rng);

            for (double lateral_offset : lateral_offsets) {
                double x = anchor.x + lateral_offset * u.x + depth_offset * v.x;
                double y = anchor.y + lateral_offset * u.y + depth_offset * v.y;

                if (!valid_candidate(
                        x,
                        y,
                        t.w,
                        t.d,
                        t.h,
                        t.gap,
                        angle,
                        sol,
                        warehouse,
                        obstacles,
                        ceiling,
                        -1,
                        &idx_grid
                    )) {
                    continue;
                }

                double q = quality_with_added(cur, t.price, t.loads, (double)t.w * t.d, wh_area);

                if (q < best_q) {
                    best_q = q;
                    best = make_candidate(t, x, y, angle);
                    found = true;
                }
            }
        }
    }

    if (!found) return false;

    sol.push_back(best);
    return true;
}

void shared_gap_refill(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx
) {
    const auto& types    = ctx.types;
    const auto& warehouse = ctx.warehouse;
    const auto& obstacles = ctx.obstacles;
    const auto& ceiling   = ctx.ceiling;
    double wh_area = ctx.wh_area;
    int mode       = ctx.mode;
    if (sol.empty()) return;

    vector<PlacedBay> backup = sol;

    int k = min(6, max(2, (int)sol.size() / 4));

    bool cluster_mode = (rng() % 4 == 0) && (int)sol.size() > k;

    if (cluster_mode) {
        int seed_idx = rng() % sol.size();
        double sx = (sol[seed_idx].bay_min_x + sol[seed_idx].bay_max_x) * 0.5;
        double sy = (sol[seed_idx].bay_min_y + sol[seed_idx].bay_max_y) * 0.5;

        vector<pair<double, int>> dist_idx;
        dist_idx.reserve(sol.size());
        for (int i = 0; i < (int)sol.size(); i++) {
            double cx = (sol[i].bay_min_x + sol[i].bay_max_x) * 0.5;
            double cy = (sol[i].bay_min_y + sol[i].bay_max_y) * 0.5;
            double dx = cx - sx, dy = cy - sy;
            dist_idx.emplace_back(dx * dx + dy * dy, i);
        }
        partial_sort(dist_idx.begin(), dist_idx.begin() + k, dist_idx.end());

        vector<int> remove_idx;
        remove_idx.reserve(k);
        for (int i = 0; i < k; i++) remove_idx.push_back(dist_idx[i].second);
        sort(remove_idx.begin(), remove_idx.end(), greater<int>());
        for (int idx : remove_idx) sol.erase(sol.begin() + idx);
    } else {
        for (int i = 0; i < k && !sol.empty(); i++) {
            int idx = rng() % sol.size();
            sol.erase(sol.begin() + idx);
        }
    }

    static const vector<int> shared_angles = {
        0, 180,
        90, 270,
        45, 225,
        135, 315
    };

    for (int i = 0; i < k + 5; i++) {
        if (!add_shared_gap_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode)) {
            add_bay(sol, ctx, shared_angles);
        }
    }

    if (quality(sol, wh_area) > quality(backup, wh_area)) {
        sol = backup;
    }
}

void rotate_compact_and_add(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx
) {
    const auto& types    = ctx.types;
    const auto& warehouse = ctx.warehouse;
    const auto& obstacles = ctx.obstacles;
    const auto& ceiling   = ctx.ceiling;
    double wh_area = ctx.wh_area;
    int mode       = ctx.mode;
    if (sol.empty()) return;

    vector<PlacedBay> backup = sol;
    double backup_q = quality(backup, wh_area);

    vector<int> order(sol.size());
    iota(order.begin(), order.end(), 0);
    shuffle(order.begin(), order.end(), rng);

    int attempts = min(4, (int)order.size());
    vector<PlacedBay> best_sol = sol;
    double best_q = backup_q;
    bool found = false;

    for (int oi = 0; oi < attempts; oi++) {
        int idx = order[oi];
        PlacedBay old = backup[idx];

        vector<PlacedBay> base = backup;
        base.erase(base.begin() + idx);

        vector<int> angles = prioritized_angles(ANGLES);
        int limit = angles.size();

        for (int ai = 0; ai < limit; ai++) {
            int angle = angles[ai];
            if (angle == old.angle) continue;

            if (!valid_candidate(
                    old.x,
                    old.y,
                    old.w,
                    old.d,
                    old.h,
                    old.gap,
                    angle,
                    base,
                    warehouse,
                    obstacles,
                    ceiling
                )) {
                continue;
            }

            vector<PlacedBay> candidate = base;
            PlacedBay rotated = old;
            rotated.angle = angle;
            refresh_cache(rotated);
            candidate.push_back(rotated);

            bool added = add_shared_gap_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
            if (!added) {
                add_bay(candidate, ctx);
            }

            double q = quality(candidate, wh_area);

            if (q < best_q) {
                best_q = q;
                best_sol = candidate;
                found = true;
            }
        }
    }

    if (found) {
        sol = best_sol;
    }
}

void add_45_degree_bay(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx
) {
    const auto& warehouse = ctx.warehouse;
    const auto& obstacles = ctx.obstacles;
    const auto& ceiling   = ctx.ceiling;
    double wh_area = ctx.wh_area;
    vector<PlacedBay> backup = sol;
    double backup_q = quality(backup, wh_area);

    static const vector<int> diagonal_angles = {45, 135, 225, 315};

    compact_diagonal_bays_on_axes(sol, warehouse, obstacles, ceiling);

    if (!add_bay(sol, ctx, diagonal_angles)) {
        sol = backup;
        return;
    }

    compact_diagonal_bays_on_axes(sol, warehouse, obstacles, ceiling);

    if (quality(sol, wh_area) > backup_q) {
        sol = backup;
    }
}

void position_perturb_and_add(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx
) {
    const auto& types    = ctx.types;
    const auto& warehouse = ctx.warehouse;
    const auto& obstacles = ctx.obstacles;
    const auto& ceiling   = ctx.ceiling;
    double wh_area = ctx.wh_area;
    int mode       = ctx.mode;
    if (sol.empty()) return;

    vector<PlacedBay> backup = sol;
    double backup_q = quality(backup, wh_area);

    vector<int> order(sol.size());
    iota(order.begin(), order.end(), 0);
    shuffle(order.begin(), order.end(), rng);

    int attempts = min(2, (int)order.size());

    static const double OFFS[] = { 100.0, 300.0 };
    static const int N_OFFS = (int)(sizeof(OFFS) / sizeof(OFFS[0]));

    vector<PlacedBay> best_sol = sol;
    double best_q = backup_q;
    bool found = false;

    for (int oi = 0; oi < attempts; oi++) {
        int idx = order[oi];
        const PlacedBay& old = backup[idx];

        for (int k = 0; k < N_OFFS; k++) {
            double mag = OFFS[k];
            double dxy[4][2] = {
                { mag, 0.0 },
                { -mag, 0.0 },
                { 0.0, mag },
                { 0.0, -mag }
            };

            for (int di = 0; di < 4; di++) {
                double new_x = old.x + dxy[di][0];
                double new_y = old.y + dxy[di][1];

                if (!valid_candidate(
                        new_x, new_y,
                        old.w, old.d, old.h, old.gap,
                        old.angle,
                        backup,
                        warehouse,
                        obstacles,
                        ceiling,
                        idx
                    )) {
                    continue;
                }

                vector<PlacedBay> candidate = backup;
                candidate[idx].x = new_x;
                candidate[idx].y = new_y;
                refresh_cache(candidate[idx]);

                if (!add_shared_gap_bay(
                        candidate, types, warehouse, obstacles, ceiling, wh_area, mode
                    )) {
                    continue;
                }

                double q = quality(candidate, wh_area);

                if (q < best_q) {
                    best_q = q;
                    best_sol = candidate;
                    found = true;
                }
            }
        }
    }

    if (found) {
        sol = best_sol;
    }
}

// ─── Operator table ───────────────────────────────────────────────────────────

static void op_add_bay            (vector<PlacedBay>& s, const OperatorContext& c) { add_bay(s, c); }
static void op_replace_bay        (vector<PlacedBay>& s, const OperatorContext& c) { replace_bay(s, c); }
static void op_fill_aggressive    (vector<PlacedBay>& s, const OperatorContext& c) { fill_aggressive(s, c); }
static void op_remove_k_and_refill(vector<PlacedBay>& s, const OperatorContext& c) { remove_k_and_refill(s, c); }
static void op_shared_gap_refill  (vector<PlacedBay>& s, const OperatorContext& c) { shared_gap_refill(s, c); }
static void op_upgrade_bay        (vector<PlacedBay>& s, const OperatorContext& c) { upgrade_bay(s, c); }
static void op_rotate_compact     (vector<PlacedBay>& s, const OperatorContext& c) { rotate_compact_and_add(s, c); }
static void op_add_45             (vector<PlacedBay>& s, const OperatorContext& c) { add_45_degree_bay(s, c); }
static void op_position_perturb   (vector<PlacedBay>& s, const OperatorContext& c) { position_perturb_and_add(s, c); }
static void op_split_bay          (vector<PlacedBay>& s, const OperatorContext& c) { split_bay(s, c); }

const OperatorEntry OPERATORS[NUM_OPERATORS] = {
    // name                    fn                        axis  angled
    {"add_bay",                op_add_bay,                4,    4},
    {"replace_bay",            op_replace_bay,            2,    2},
    {"fill_aggressive",        op_fill_aggressive,        0,    0},
    {"remove_k_and_refill",    op_remove_k_and_refill,    6,    6},
    {"shared_gap_refill",      op_shared_gap_refill,      8,    8},
    {"upgrade_bay",            op_upgrade_bay,            4,    4},
    {"rotate_compact_and_add", op_rotate_compact,         1,    4},
    {"add_45_degree_bay",      op_add_45,                 1,    2},
    {"position_perturb_and_add",op_position_perturb,     2,    1},
    {"split_bay",              op_split_bay,              2,    2},
};

vector<int> build_active_ops(bool axis_aligned) {
    vector<int> ops;
    for (int i = 0; i < NUM_OPERATORS; i++) {
        int w = axis_aligned ? OPERATORS[i].weight_axis : OPERATORS[i].weight_angled;
        for (int j = 0; j < w; j++) ops.push_back(i);
    }
    return ops;
}

// ─── OperatorSelector ────────────────────────────────────────────────────────

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

    auto sa_start = Clock::now();
    double total_seconds = chrono::duration<double>(deadline - sa_start).count();
    if (total_seconds <= 0.0) total_seconds = 1.0;

    vector<PlacedBay> current = sol;
    double current_q = quality(current, wh_area);

    vector<PlacedBay> best = current;
    double best_q = current_q;

    double T0 = max(20.0, t0_frac * current_q);

    OperatorSelector sel(build_active_ops(axis_aligned));

    int iter = 0;
    while (Clock::now() < deadline && iter < ITERATIONS) {
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

void print_operator_stats(const string& case_dir, const OperatorStats& stats) {
    cout << "\nOperator stats for " << case_dir << ":\n";

    for (int i = 0; i < NUM_OPERATORS; i++) {
        double rate = 0.0;

        if (stats.tried[i] > 0) {
            rate = 100.0 * stats.improved[i] / stats.tried[i];
        }

        cout << OPERATORS[i].name
             << " | tried=" << stats.tried[i]
             << " | improved=" << stats.improved[i]
             << " | rate=" << rate << "%\n";
    }
}

// ================= SOLVER CORE =================

bool is_valid_solution(
    const vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling
) {
    for (int i = 0; i < (int)sol.size(); i++) {
        const auto& p = sol[i];

        if (!valid_candidate(
                p.x,
                p.y,
                p.w,
                p.d,
                p.h,
                p.gap,
                p.angle,
                sol,
                warehouse,
                obstacles,
                ceiling,
                i
            )) {
            return false;
        }
    }

    return true;
}

bool warehouse_is_axis_aligned(const vector<Point>& warehouse) {
    for (int i = 0; i < (int)warehouse.size(); i++) {
        Point a = warehouse[i];
        Point b = warehouse[(i + 1) % warehouse.size()];
        double dx = fabs(b.x - a.x);
        double dy = fabs(b.y - a.y);
        if (dx > EPS && dy > EPS) return false;
    }
    return true;
}

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
) {
    Bounds wh_bb = polygon_bounds(warehouse);

    vector<BayType> sorted = types;
    sort(sorted.begin(), sorted.end(), [&](const BayType& a, const BayType& b) {
        return bay_score_mode(a, mode, wh_area) > bay_score_mode(b, mode, wh_area);
    });

    const double X_STEP = 100.0;
    const double Y_SKIP = 200.0;
    const int MAX_ROWS = 500;
    const int MAX_PER_ROW = 1000;

    auto try_place = [&](double x, double y, const BayType& t, int o,
                         double& out_ax, double& out_ay, int& out_angle) -> bool {
        double ax = x;
        double ay = (o == 0) ? y : (y + t.w);
        int angle = (o == 0) ? 0 : 270;
        if (valid_candidate(ax, ay, t.w, t.d, t.h, t.gap, angle,
                            sol, warehouse, obstacles, ceiling)) {
            out_ax = ax; out_ay = ay; out_angle = angle;
            return true;
        }
        return false;
    };

    double y = wh_bb.min_y + off_y;
    int rows = 0;

    while (y < wh_bb.max_y && rows < MAX_ROWS) {
        rows++;
        double x = wh_bb.min_x + off_x;
        double max_row_dim = 0.0;
        bool placed_any_in_row = false;
        int safety = 0;

        while (x < wh_bb.max_x && safety++ < MAX_PER_ROW) {
            bool placed = false;
            for (auto& t : sorted) {
                double ax, ay; int angle;
                int placed_orient = -1;
                if (try_place(x, y, t, orientation, ax, ay, angle)) {
                    placed_orient = orientation;
                } else if (try_place(x, y, t, 1 - orientation, ax, ay, angle)) {
                    placed_orient = 1 - orientation;
                }
                if (placed_orient >= 0) {
                    sol.push_back(make_candidate(t, ax, ay, angle));
                    if (placed_orient == 0) {
                        x += t.w;
                        max_row_dim = max(max_row_dim, (double)t.d + t.gap);
                    } else {
                        x += (double)t.d + t.gap;
                        max_row_dim = max(max_row_dim, (double)t.w);
                    }
                    placed = true;
                    placed_any_in_row = true;
                    break;
                }
            }
            if (!placed) {
                x += X_STEP;
            }
        }

        if (!placed_any_in_row) {
            y += Y_SKIP;
            continue;
        }
        y += max_row_dim;
    }
}

vector<PlacedBay> build_initial(
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode,
    bool axis_aligned,
    int /*strategy*/
) {
    vector<PlacedBay> sol;

    if (axis_aligned) {
        int min_w = INT_MAX;
        for (auto& t : types) min_w = min(min_w, t.w);
        double off = min_w / 2.0;

        struct Variant { int orientation; double off_x; double off_y; };
        vector<Variant> variants = {
            {0, 0.0, 0.0},
            {1, 0.0, 0.0},
            {0, off, 0.0},
            {1, 0.0, off}
        };

        double best_q = 1e100;
        vector<PlacedBay> best;
        for (auto& v : variants) {
            vector<PlacedBay> tmp;
            shelf_pack_into(tmp, types, warehouse, obstacles, ceiling, wh_area, mode,
                            v.orientation, v.off_x, v.off_y);
            double q = quality(tmp, wh_area);
            if (q < best_q) {
                best_q = q;
                best = tmp;
            }
        }
        sol = best;
    }

    for (int i = 0; i < INITIAL_ADDS; i++) {
        OperatorContext ctx{types, warehouse, obstacles, ceiling, wh_area, mode};
        if (!add_bay(sol, ctx)) {
            break;
        }
    }

    return sol;
}

double seconds_since(Clock::time_point start) {
    return chrono::duration<double>(Clock::now() - start).count();
}

void solve_case(const string& case_dir) {
    auto case_start = Clock::now();

    cout << "=== Solving " << case_dir << " ===\n";

    auto warehouse = read_warehouse(case_dir + "/warehouse.csv");
    auto obstacles = read_obstacles(case_dir + "/obstacles.csv");
    auto ceiling = read_ceiling(case_dir + "/ceiling.csv");
    auto types = read_bays(case_dir + "/types_of_bays.csv");

    double wh_area = available_warehouse_area(warehouse, obstacles);
    bool axis_aligned = warehouse_is_axis_aligned(warehouse);

    auto deadline = Clock::now() + chrono::milliseconds((long long)(CASE_BUDGET_SECONDS * 1000.0));

    struct RestartConfig { bool use_sa; double t0_frac; };
    static const array<RestartConfig, 10> RESTART_TIERS = {{
        {false, 0.00}, {false, 0.00},
        {true,  0.03}, {true,  0.03},
        {true,  0.05}, {true,  0.05}, {true, 0.05}, {true, 0.05},
        {true,  0.08}, {true,  0.08},
    }};

    auto worker = [&](int r) {
        int mode = r % 4;

        rng.seed(42 + r * 1000 + (int)(hash<string>{}(case_dir) % 100000));

        int strategy = r % 4;
        auto sol = build_initial(types, warehouse, obstacles, ceiling, wh_area, mode, axis_aligned, strategy);

        OperatorStats stats;

        const auto& rc = RESTART_TIERS[r];
        if (!rc.use_sa) {
            sol = hill(sol, types, warehouse, obstacles, ceiling, wh_area, mode, stats, axis_aligned, deadline);
        } else {
            sol = hill_sa(sol, types, warehouse, obstacles, ceiling, wh_area, mode, stats, axis_aligned, deadline, rc.t0_frac);
        }

        return make_pair(sol, stats);
    };

    vector<future<pair<vector<PlacedBay>, OperatorStats>>> futures;

    for (int r = 0; r < RESTARTS; r++) {
        futures.push_back(async(launch::async, worker, r));
    }

    vector<PlacedBay> best_global;
    double best_global_q = 1e100;

    for (int r = 0; r < RESTARTS; r++) {
        auto result = futures[r].get();

        vector<PlacedBay> sol = result.first;

        auto [a, l, pr, q] = details(sol, wh_area);
        bool valid = is_valid_solution(sol, warehouse, obstacles, ceiling);

        if (valid && q < best_global_q) {
            best_global = sol;
            best_global_q = q;
        }
    }

    if (!is_valid_solution(best_global, warehouse, obstacles, ceiling)) {
        best_global.clear();
    }

    string out_path = case_dir + "/solution.csv";
    ofstream out(out_path);

    out << "Id,X,Y,Rotation\n";

    for (auto& p : best_global) {
        out << p.id << ","
            << llround(p.x) << ","
            << llround(p.y) << ","
            << p.angle << "\n";
    }

    out.close();

    auto [a, l, pr, q] = details(best_global, wh_area);

    cout << "BEST bays=" << best_global.size()
         << " area=" << a
         << " loads=" << l
         << " price=" << pr
         << " Q=" << q
         << " elapsed=" << seconds_since(case_start) << "s"
         << " -> " << out_path << "\n";
}
