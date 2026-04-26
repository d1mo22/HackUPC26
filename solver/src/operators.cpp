#include "operators.h"

// ---- helpers ----

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

    // Build spatial index once over sol — anchor loop tries up to
    // SHARED_GAP_MAX_ANCHORS * N_types * 3 valid_candidate calls against the
    // unchanged sol, so amortising the build is a clear win.
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

// ---- operator functions ----

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

    // Build spatial index once over the (already-erased) sol — every type/angle
    // combination tested below queries against the same set, so the build cost
    // is amortised across N_types * ANGLE_SAMPLE valid_candidate calls.
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

// split_bay: removes one bay and tries to fill its (axis-aligned) bbox with
// multiple smaller bays. The operator wins primarily by increasing total bay
// area: Q = (Σp/Σl)^(2 - area_ratio) is monotonically improved by adding area
// when the base is > 1 (typical). For axis-aligned victims, the bbox equals the
// bay footprint, so we can rarely fit more area inside — operator is mostly
// dead. For *rotated* victims (angle not 0/90/180/270), the bbox is larger
// than w×d, leaving corner pockets that smaller axis-aligned bays may fill.
// Therefore the operator is gated to rotated victims only.
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

    // Find rotated bays (angle not aligned to 0/90/180/270). If none, return.
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

    // Need meaningful pocket space (bbox materially larger than bay footprint).
    if (v_bbox_area < v_bay_area * 1.15) return;

    // Smaller types — anything that could plausibly fit a corner pocket.
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

    // Reject if ≤1 placed (then it's just replace_bay) or Q didn't improve.
    if (placed < 2 || quality(sol, wh_area) >= backup_q) {
        sol = backup;
    }
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

    // Cluster removal: 25% chance to remove the k bays nearest to a randomly
    // chosen seed (contiguous spatial void → larger refill opportunities), else
    // fall back to scattered random removal (broader exploration). Empirically
    // 50% over-rotated towards cluster mode and hurt angled cases; 25% gives
    // cluster a meaningful presence without dominating the operator's behavior.
    bool cluster_mode = (rng() % 4 == 0) && (int)sol.size() > k;

    if (cluster_mode) {
        int seed_idx = rng() % sol.size();
        // Center of seed bay
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
        // Partial sort to find k nearest
        partial_sort(dist_idx.begin(), dist_idx.begin() + k, dist_idx.end());

        // Collect indices to remove, sort descending, erase in-place
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

// position_perturb_and_add (op 8):
// Pick a random bay, try shifting it by small/medium offsets along x and y axes.
// If the shift is valid, attempt to add a new bay (preferring shared-gap, then add_bay).
// Accept the new state only if Q improves vs the original solution.
//
// Symmetric counterpart to compact_diagonal_bays_on_axes but for orthogonal bays:
// the goal is to nudge a bay slightly to free a pocket large enough for one
// extra placement. Position-only moves don't change Q themselves, so the win
// has to come from the follow-up add_bay.
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

    // Offsets to try, in mm. Tried as ±dx and ±dy for each magnitude.
    // Kept small: large offsets rarely help (the bay was placed where it was
    // for a reason) and each tried offset costs a valid_candidate call.
    static const double OFFS[] = { 100.0, 300.0 };
    static const int N_OFFS = (int)(sizeof(OFFS) / sizeof(OFFS[0]));

    vector<PlacedBay> best_sol = sol;
    double best_q = backup_q;
    bool found = false;

    for (int oi = 0; oi < attempts; oi++) {
        int idx = order[oi];
        const PlacedBay& old = backup[idx];

        // 4 directions × 2 magnitudes = 8 candidate positions per bay.
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

                // Validate the moved bay against everything except itself.
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

                // Build candidate: backup with the moved bay.
                vector<PlacedBay> candidate = backup;
                candidate[idx].x = new_x;
                candidate[idx].y = new_y;
                refresh_cache(candidate[idx]);

                // Use the cheap anchor-based add (shared-gap) only.
                // The full add_bay was too expensive and dominated cost.
                if (!add_shared_gap_bay(
                        candidate, types, warehouse, obstacles, ceiling, wh_area, mode
                    )) {
                    continue;  // perturb without follow-up add: Q unchanged, skip
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

// Index must match OP_NAMES and the op == N checks in operator stats.
const OperatorEntry OPERATORS[NUM_OPERATORS] = {
    // name                    fn                        axis  angled
    {"add_bay",                op_add_bay,                4,    4},
    {"replace_bay",            op_replace_bay,            2,    2},
    {"fill_aggressive",        op_fill_aggressive,        0,    0},  // weight 0 = never in active_ops
    {"remove_k_and_refill",    op_remove_k_and_refill,    6,    6},
    {"shared_gap_refill",      op_shared_gap_refill,      8,    8},
    {"upgrade_bay",            op_upgrade_bay,            4,    4},
    {"rotate_compact_and_add", op_rotate_compact,         1,    4},
    {"add_45_degree_bay",      op_add_45,                 1,    2},
    {"position_perturb_and_add",op_position_perturb,     2,    1},
    {"split_bay",              op_split_bay,              2,    2},
};

// Replaces the old imperative build_active_ops: reads weights from OPERATORS[].
vector<int> build_active_ops(bool axis_aligned) {
    vector<int> ops;
    for (int i = 0; i < NUM_OPERATORS; i++) {
        int w = axis_aligned ? OPERATORS[i].weight_axis : OPERATORS[i].weight_angled;
        for (int j = 0; j < w; j++) ops.push_back(i);
    }
    return ops;
}
