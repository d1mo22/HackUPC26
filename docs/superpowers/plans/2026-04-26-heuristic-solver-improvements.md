# Heuristic Solver Improvements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve `solver/solver_heuristic.cpp` by adding interior grid candidate points, lookahead-1 scoring, relaxed Q filter, and a deadline-driven post-construction improvement loop (upgrade_pass + fill_pass + remove_k_refill).

**Architecture:** All changes in `solver/solver_heuristic.cpp` only. Construction phase gets three enhancements. A new `improve()` function runs after construction for each variant, looping upgrade→fill→remove_k until deadline or convergence. Budget = 25s / n_existing_cases, computed in `main()` and passed through `solve_case()`.

**Tech Stack:** C++17, OpenMP (libomp via Homebrew on macOS).

**Spec:** `docs/superpowers/specs/2026-04-26-heuristic-solver-improvements-design.md`

**Baseline Q (current heuristic, minimize):**
- Case0: 3242, Case1: 5011, Case2: 6949, Case3: 11335

**Current file:** `solver/solver_heuristic.cpp` (1228 lines). Key line ranges:
- Line 1: constants (`EPS`, `CASES`)
- Line 823: `CornerPoint` struct and corner-point functions
- Line 907: `Variant`, `ScoreNorms`, score functions
- Line 1021: sweep/sort helpers
- Line 1088: `construct_variant` (lines 1090–1151)
- Line 1153: `all_variants()`
- Line 1173: `solve_case()`
- Line 1219: `main()`

---

## File Structure

- **Modify only:** `solver/solver_heuristic.cpp`
  - Add constants block (Task 1)
  - Add `interior_grid_points()` (Task 2)
  - Add `count_neighbors()` and modify `construct_variant()` (Task 3)
  - Add `upgrade_pass()`, `fill_pass()`, `remove_k_refill()`, `improve()` (Task 4)
  - Replace `solve_case()` and `main()` (Task 5)

---

## Task 1: Add new constants

**Files:**
- Modify: `solver/solver_heuristic.cpp` (top of file, after existing constants)

- [ ] **Step 1: Find the existing constants at the top of the file and add new ones after them.**

The file currently has (around lines 1–30):
```cpp
const vector<string> CASES = { ... };
const double EPS = 1e-7;
```

Add immediately after `const double EPS = 1e-7;`:

```cpp
// ============== IMPROVEMENT CONSTANTS ==============
const double TOTAL_BUDGET_SECONDS = 25.0;
const int    LOOKAHEAD_K          = 10;
const int    INTERIOR_GRID_CAP    = 300;
const double RELAX_FACTOR         = 0.05;
const double W_LOOKAHEAD          = 0.3;
```

- [ ] **Step 2: Compile to confirm no errors introduced.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp
```

Expected: clean compile (warnings OK, errors not OK).

- [ ] **Step 3: Commit.**

```bash
cd /Users/david.morais/HackUPC26
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): add improvement constants (budget, lookahead, grid, relax)"
```

---

## Task 2: Interior grid candidate points

**Files:**
- Modify: `solver/solver_heuristic.cpp` — add `interior_grid_points()` after `filter_corners_inside_envelope()` (currently around line 905)

- [ ] **Step 1: Add `INTERIOR_GRID` origin to `CornerPoint::Origin` enum.**

Find the `CornerPoint` struct (around line 825):
```cpp
struct CornerPoint {
    double x, y;
    enum Origin {
        WAREHOUSE_BBOX, CONCAVE_VERTEX,
        BAY_TL, BAY_TR, BAY_BL, BAY_BR,
        GAP_TL, GAP_TR, GAP_BL, GAP_BR
    } origin;
};
```

Replace it with:
```cpp
struct CornerPoint {
    double x, y;
    enum Origin {
        WAREHOUSE_BBOX, CONCAVE_VERTEX,
        BAY_TL, BAY_TR, BAY_BL, BAY_BR,
        GAP_TL, GAP_TR, GAP_BL, GAP_BR,
        INTERIOR_GRID
    } origin;
};
```

- [ ] **Step 2: Add `interior_grid_points()` after `filter_corners_inside_envelope()` (around line 905, before `// ============== VARIANT + SCORE ==============`).**

```cpp
// Generates a regular grid of candidate points inside the warehouse polygon,
// excluding obstacle interiors. Cap at INTERIOR_GRID_CAP points.
vector<CornerPoint> interior_grid_points(
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<BayType>& types
) {
    if (types.empty()) return {};

    // Step = half the smallest bay dimension across all types, clamped.
    int min_dim = INT_MAX;
    for (auto& t : types) {
        min_dim = min(min_dim, min(t.w, t.d));
    }
    double step = max(500.0, min(5000.0, min_dim / 2.0));

    Bounds bb = polygon_bounds(warehouse);
    vector<CornerPoint> out;

    for (double x = bb.min_x; x <= bb.max_x + EPS && (int)out.size() < INTERIOR_GRID_CAP; x += step) {
        for (double y = bb.min_y; y <= bb.max_y + EPS && (int)out.size() < INTERIOR_GRID_CAP; y += step) {
            Point p{x, y};
            if (!point_inside_or_on_polygon(p, warehouse)) continue;
            bool in_obstacle = false;
            for (auto& o : obstacles) {
                if (x > o.x + EPS && x < o.x + o.w - EPS &&
                    y > o.y + EPS && y < o.y + o.d - EPS) {
                    in_obstacle = true;
                    break;
                }
            }
            if (!in_obstacle) {
                out.push_back({x, y, CornerPoint::INTERIOR_GRID});
            }
        }
    }
    return out;
}
```

- [ ] **Step 3: Compile.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp
```

Expected: clean compile.

- [ ] **Step 4: Add interior grid points to `construct_variant()`.**

Find `construct_variant()` (around line 1090). The current opening is:
```cpp
    ScoreNorms norms = compute_norms(types, ceiling);
    vector<int> type_order = sort_types(types, v.type_prio);
    vector<PlacedBay> sol;
    vector<CornerPoint> corners = initial_corner_points(warehouse);
```

Replace that block with:
```cpp
    ScoreNorms norms = compute_norms(types, ceiling);
    vector<int> type_order = sort_types(types, v.type_prio);
    vector<PlacedBay> sol;
    vector<CornerPoint> corners = initial_corner_points(warehouse);
    // Add interior grid points once at the start (not regenerated per iteration).
    auto grid_pts = interior_grid_points(warehouse, obstacles, types);
    for (auto& gp : grid_pts) corners.push_back(gp);
```

- [ ] **Step 5: Compile and run a quick smoke test.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp
cd /Users/david.morais/HackUPC26/solver && /tmp/solver_heuristic 2>&1 | grep "BEST\|Solving"
```

Expected: BEST lines for Case0–Case3 with Q values. They should be equal or better than before (3242/5011/6949/11335). If they're slightly worse on some cases that's acceptable — the improvement loop (Task 4) will fix it.

- [ ] **Step 6: Commit.**

```bash
cd /Users/david.morais/HackUPC26
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): interior grid candidate points in construct_variant"
```

---

## Task 3: Lookahead-1 + relaxed Q filter in construction

**Files:**
- Modify: `solver/solver_heuristic.cpp` — add `count_neighbors()`, rewrite inner loop of `construct_variant()`

- [ ] **Step 1: Add `count_neighbors()` after `interior_grid_points()` (before `// ============== VARIANT + SCORE ==============`).**

```cpp
// Counts how many valid (type, rotation) placements exist at any corner point
// within `radius` of `cand`'s bounding box center, given sol already contains cand.
// Used for lookahead-1 scoring.
int count_neighbors(
    const vector<PlacedBay>& sol_with_cand,  // sol already includes the candidate
    const PlacedBay& cand,
    double radius,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling
) {
    double cx = (cand.bay_min_x + cand.bay_max_x) / 2.0;
    double cy = (cand.bay_min_y + cand.bay_max_y) / 2.0;

    // Generate candidate corner points from the placed cand.
    auto cand_corners = corners_from_placed_bay(cand);

    // Also add initial warehouse corners (to count wall placements).
    auto wh_corners = initial_corner_points(warehouse);
    for (auto& wc : wh_corners) cand_corners.push_back(wc);

    SpatialIndex idx;
    idx.build(sol_with_cand);

    int count = 0;
    for (auto& cp : cand_corners) {
        double dx = cp.x - cx, dy = cp.y - cy;
        if (dx*dx + dy*dy > radius*radius) continue;
        for (auto& t : types) {
            for (int rot = 0; rot < 2; rot++) {
                PlacedBay nb = make_candidate_axis_aligned(t, cp.x, cp.y, rot);
                if (valid_candidate(nb.x, nb.y, nb.w, nb.d, nb.h, nb.gap,
                                    nb.angle, sol_with_cand, warehouse,
                                    obstacles, ceiling, -1, &idx)) {
                    count++;
                }
            }
        }
    }
    return count;
}
```

- [ ] **Step 2: Rewrite the inner scoring loop of `construct_variant()` to add lookahead and relaxed Q filter.**

Find the `while (true)` loop body in `construct_variant()`. The current inner loop looks like:

```cpp
    while (true) {
        sort_corners(corners, v, warehouse);
        QSums sums = compute_sums(sol);
        double Q_now = sol.empty() ? 1e100 : quality_from_sums(sums, wh_area, false);

        struct Best { double score; int cp_idx; int rot; PlacedBay bay; };
        Best best{-1e100, -1, -1, {}};

        // Build spatial index from current solution for fast collision checks
        SpatialIndex idx;
        idx.build(sol);

        for (int ci = 0; ci < (int)corners.size(); ci++) {
            const CornerPoint& cp = corners[ci];
            for (int ti : type_order) {
                const BayType& t = types[ti];
                for (int rot = 0; rot < 2; rot++) {
                    PlacedBay cand = make_candidate_axis_aligned(t, cp.x, cp.y, rot);
                    if (!valid_candidate(cand.x,cand.y,cand.w,cand.d,cand.h,cand.gap,
                                         cand.angle,sol,warehouse,obstacles,ceiling,-1,&idx))
                        continue;
                    double bay_area = (double)cand.w * cand.d;
                    double Q_new = quality_with_added(sums, cand.price, cand.loads, bay_area, wh_area);
                    if (!sol.empty() && Q_new >= Q_now - EPS) continue;
                    double s = score_candidate(cand,v,norms,sol,warehouse,obstacles,ceiling);
                    bool win = (s > best.score + EPS);
                    if (!win && fabs(s - best.score) <= EPS) {
                        if (best.cp_idx < 0) win = true;
                        else if (cp.x+cp.y < corners[best.cp_idx].x+corners[best.cp_idx].y - EPS) win = true;
                        else if (fabs(cp.x+cp.y-(corners[best.cp_idx].x+corners[best.cp_idx].y)) <= EPS) {
                            if (t.id < best.bay.id) win = true;
                            else if (t.id == best.bay.id && rot < best.rot) win = true;
                        }
                    }
                    if (win) best = {s, ci, rot, cand};
                }
            }
        }

        if (best.cp_idx < 0) break;

        sol.push_back(best.bay);
        auto new_corners = corners_from_placed_bay(best.bay);
        for (auto& nc : new_corners) corners.push_back(nc);
        filter_corners_inside_envelope(corners, best.bay);
    }
```

Replace the entire `while (true)` body with:

```cpp
    int iteration = 0;
    int N_relax = max(5, (int)types.size());
    // Max bay dimension for lookahead radius.
    int max_bay_dim = 1;
    for (auto& t : types) max_bay_dim = max(max_bay_dim, max(t.w + t.gap, t.d + t.gap));
    double lookahead_radius = 2.0 * max_bay_dim;

    while (true) {
        sort_corners(corners, v, warehouse);
        QSums sums = compute_sums(sol);
        double Q_now = sol.empty() ? 1e100 : quality_from_sums(sums, wh_area, false);

        struct CandInfo { double base_score; int ci; int ti; int rot; PlacedBay bay; };
        vector<CandInfo> valid_cands;

        SpatialIndex idx;
        idx.build(sol);

        bool use_relax = (!sol.empty() && iteration < N_relax);

        for (int ci = 0; ci < (int)corners.size(); ci++) {
            const CornerPoint& cp = corners[ci];
            for (int ti : type_order) {
                const BayType& t = types[ti];
                for (int rot = 0; rot < 2; rot++) {
                    PlacedBay cand = make_candidate_axis_aligned(t, cp.x, cp.y, rot);
                    if (!valid_candidate(cand.x,cand.y,cand.w,cand.d,cand.h,cand.gap,
                                         cand.angle,sol,warehouse,obstacles,ceiling,-1,&idx))
                        continue;
                    double bay_area = (double)cand.w * cand.d;
                    double Q_new = quality_with_added(sums, cand.price, cand.loads, bay_area, wh_area);
                    // Q filter: strict after N_relax iterations, relaxed before.
                    bool passes_q;
                    if (sol.empty()) {
                        passes_q = true;
                    } else if (use_relax) {
                        passes_q = (Q_new <= Q_now * (1.0 + RELAX_FACTOR));
                    } else {
                        passes_q = (Q_new < Q_now - EPS);
                    }
                    if (!passes_q) continue;
                    double s = score_candidate(cand,v,norms,sol,warehouse,obstacles,ceiling);
                    valid_cands.push_back({s, ci, ti, rot, cand});
                }
            }
        }

        if (valid_cands.empty()) break;

        // Sort by base_score descending; take top LOOKAHEAD_K for lookahead refinement.
        sort(valid_cands.begin(), valid_cands.end(),
             [](const CandInfo& a, const CandInfo& b){ return a.base_score > b.base_score; });

        int lookahead_n = min(LOOKAHEAD_K, (int)valid_cands.size());
        int max_neighbors = 0;

        // First pass: collect neighbor counts for top-K.
        vector<int> neighbor_counts(lookahead_n, 0);
        for (int i = 0; i < lookahead_n; i++) {
            vector<PlacedBay> sol_tmp = sol;
            sol_tmp.push_back(valid_cands[i].bay);
            neighbor_counts[i] = count_neighbors(sol_tmp, valid_cands[i].bay,
                                                  lookahead_radius, types,
                                                  warehouse, obstacles, ceiling);
            max_neighbors = max(max_neighbors, neighbor_counts[i]);
        }

        // Second pass: compute final scores with lookahead for top-K.
        double best_score = -1e100;
        int best_idx = 0;
        for (int i = 0; i < (int)valid_cands.size(); i++) {
            double final_score = valid_cands[i].base_score;
            if (i < lookahead_n && max_neighbors > 0) {
                double S_la = (double)neighbor_counts[i] / max_neighbors;
                final_score += W_LOOKAHEAD * S_la;
            }
            // Deterministic tie-break: smaller (x+y), then type_id, then rot.
            bool win = (final_score > best_score + EPS);
            if (!win && fabs(final_score - best_score) <= EPS) {
                const CandInfo& cur  = valid_cands[i];
                const CandInfo& prev = valid_cands[best_idx];
                double cur_xy  = corners[cur.ci].x  + corners[cur.ci].y;
                double prev_xy = corners[prev.ci].x + corners[prev.ci].y;
                if (cur_xy < prev_xy - EPS) win = true;
                else if (fabs(cur_xy - prev_xy) <= EPS) {
                    if (cur.bay.id < prev.bay.id) win = true;
                    else if (cur.bay.id == prev.bay.id && cur.rot < prev.rot) win = true;
                }
            }
            if (win) { best_score = final_score; best_idx = i; }
        }

        const CandInfo& chosen = valid_cands[best_idx];
        sol.push_back(chosen.bay);
        auto new_corners = corners_from_placed_bay(chosen.bay);
        for (auto& nc : new_corners) corners.push_back(nc);
        filter_corners_inside_envelope(corners, chosen.bay);
        iteration++;
    }
```

- [ ] **Step 3: Compile.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp
```

Expected: clean compile.

- [ ] **Step 4: Run and check Q values vs baseline (3242/5011/6949/11335).**

```bash
cd /Users/david.morais/HackUPC26/solver && /tmp/solver_heuristic 2>&1 | grep "BEST"
```

Expected: Q values equal or better than before on Case0–Case3. Construction may be slower (lookahead). If any case takes > 10s, that's a problem — report as DONE_WITH_CONCERNS.

- [ ] **Step 5: Commit.**

```bash
cd /Users/david.morais/HackUPC26
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): lookahead-1 scoring + relaxed Q filter in construct_variant"
```

---

## Task 4: Post-construction improvement operators

**Files:**
- Modify: `solver/solver_heuristic.cpp` — add `upgrade_pass()`, `fill_pass()`, `remove_k_refill()`, `improve()` before `solve_case()`

Add all four functions after `all_variants()` (around line 1170), before `void solve_case(...)`.

- [ ] **Step 1: Add `upgrade_pass()`.**

```cpp
// ============== IMPROVEMENT OPERATORS ==============

// Tries to replace each bay in sol with a better type/rotation at the same position.
// Returns true if any bay was improved.
bool upgrade_pass(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area
) {
    bool improved = false;
    QSums sums = compute_sums(sol);
    for (int i = 0; i < (int)sol.size(); i++) {
        double Q_best = quality_from_sums(sums, wh_area, false);
        PlacedBay best_bay = sol[i];
        QSums best_sums = sums;
        for (auto& t : types) {
            for (int rot = 0; rot < 2; rot++) {
                PlacedBay cand = make_candidate_axis_aligned(t, sol[i].x, sol[i].y, rot);
                if (!valid_candidate(cand.x, cand.y, cand.w, cand.d, cand.h, cand.gap,
                                     cand.angle, sol, warehouse, obstacles, ceiling, i))
                    continue;
                // Compute Q with sol[i] replaced by cand.
                QSums trial = sums;
                trial.price -= sol[i].price;
                trial.loads -= sol[i].loads;
                trial.area  -= (double)sol[i].w * sol[i].d;
                trial.price += cand.price;
                trial.loads += cand.loads;
                trial.area  += (double)cand.w * cand.d;
                double Q_new = quality_from_sums(trial, wh_area, false);
                if (Q_new < Q_best - EPS) {
                    Q_best = Q_new;
                    best_bay = cand;
                    best_sums = trial;
                }
            }
        }
        if (best_bay.id != sol[i].id || best_bay.angle != sol[i].angle ||
            best_bay.w != sol[i].w || best_bay.d != sol[i].d) {
            sol[i] = best_bay;
            sums = best_sums;
            improved = true;
        }
    }
    return improved;
}
```

- [ ] **Step 2: Add `fill_pass()`.**

```cpp
// Adds as many bays as possible to the existing solution using the greedy
// construction heuristic (strict Q filter, no lookahead, no relaxation).
// Uses variant v01 criteria (LOWEST_LEFT, BY_PRICE_PER_LOAD).
// Returns true if at least one bay was added.
bool fill_pass(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area
) {
    if (types.empty()) return false;

    ScoreNorms norms = compute_norms(types, ceiling);
    // Fixed fill variant: LOWEST_LEFT, BY_PRICE_PER_LOAD, seed BL.
    Variant fill_v{SweepOrder::LOWEST_LEFT, TypePrio::BY_PRICE_PER_LOAD, 0,
                   1.0, 0.5, 0.3, 0.3, 0.2, "fill"};
    vector<int> type_order = sort_types(types, fill_v.type_prio);

    // Rebuild corner points from scratch for current sol.
    vector<CornerPoint> corners = initial_corner_points(warehouse);
    for (auto& p : sol) {
        auto cp = corners_from_placed_bay(p);
        for (auto& c : cp) corners.push_back(c);
    }

    bool any_added = false;

    while (true) {
        sort_corners(corners, fill_v, warehouse);
        QSums sums = compute_sums(sol);
        double Q_now = sol.empty() ? 1e100 : quality_from_sums(sums, wh_area, false);

        struct Best { double score; int ci; int rot; PlacedBay bay; };
        Best best{-1e100, -1, -1, {}};

        SpatialIndex idx;
        idx.build(sol);

        for (int ci = 0; ci < (int)corners.size(); ci++) {
            const CornerPoint& cp = corners[ci];
            for (int ti : type_order) {
                const BayType& t = types[ti];
                for (int rot = 0; rot < 2; rot++) {
                    PlacedBay cand = make_candidate_axis_aligned(t, cp.x, cp.y, rot);
                    if (!valid_candidate(cand.x,cand.y,cand.w,cand.d,cand.h,cand.gap,
                                         cand.angle,sol,warehouse,obstacles,ceiling,-1,&idx))
                        continue;
                    double bay_area = (double)cand.w * cand.d;
                    double Q_new = quality_with_added(sums, cand.price, cand.loads, bay_area, wh_area);
                    if (!sol.empty() && Q_new >= Q_now - EPS) continue;
                    double s = score_candidate(cand,fill_v,norms,sol,warehouse,obstacles,ceiling);
                    bool win = (s > best.score + EPS);
                    if (!win && fabs(s - best.score) <= EPS) {
                        if (best.ci < 0) win = true;
                        else if (cp.x+cp.y < corners[best.ci].x+corners[best.ci].y - EPS) win = true;
                        else if (fabs(cp.x+cp.y-(corners[best.ci].x+corners[best.ci].y)) <= EPS) {
                            if (t.id < best.bay.id) win = true;
                            else if (t.id == best.bay.id && rot < best.rot) win = true;
                        }
                    }
                    if (win) best = {s, ci, rot, cand};
                }
            }
        }

        if (best.ci < 0) break;
        sol.push_back(best.bay);
        auto nc = corners_from_placed_bay(best.bay);
        for (auto& c : nc) corners.push_back(c);
        filter_corners_inside_envelope(corners, best.bay);
        any_added = true;
    }
    return any_added;
}
```

- [ ] **Step 3: Add `remove_k_refill()`.**

```cpp
// Removes k bays and tries to refill. Returns true if Q improved.
// k=1: try each bay individually (worst ΔQ first).
// k=2: try spatially adjacent pairs (cap 30 pairs).
// k=3: remove 3 worst-ΔQ bays together, try refill once.
bool remove_k_refill(
    vector<PlacedBay>& sol,
    int k,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area
) {
    if ((int)sol.size() < k) return false;
    bool improved = false;

    // Compute per-bay ΔQ contribution (how much Q changes if we remove bay i).
    QSums full_sums = compute_sums(sol);
    double Q_full = quality_from_sums(full_sums, wh_area, false);

    auto q_without = [&](int i) -> double {
        QSums s = full_sums;
        s.price -= sol[i].price;
        s.loads -= sol[i].loads;
        s.area  -= (double)sol[i].w * sol[i].d;
        if (s.price <= 0 && s.loads <= 0) return 1e100;
        return quality_from_sums(s, wh_area, s.price <= 0);
    };

    // Sort indices by ΔQ ascending (removing these bays hurts Q least → best candidates for removal).
    vector<int> order(sol.size());
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b){
        return q_without(a) < q_without(b);
    });

    if (k == 1) {
        for (int idx : order) {
            vector<PlacedBay> trial = sol;
            trial.erase(trial.begin() + idx);
            fill_pass(trial, types, warehouse, obstacles, ceiling, wh_area);
            double Q_trial = quality(trial, wh_area);
            if (Q_trial < Q_full - EPS) {
                sol = trial;
                full_sums = compute_sums(sol);
                Q_full = Q_trial;
                improved = true;
                // Recompute order after change (sol changed size).
                order.resize(sol.size());
                iota(order.begin(), order.end(), 0);
                sort(order.begin(), order.end(), [&](int a, int b){
                    return q_without(a) < q_without(b);
                });
            }
        }
    } else if (k == 2) {
        // Try spatially adjacent pairs only. Build proximity list.
        vector<pair<int,int>> pairs;
        for (int i = 0; i < (int)sol.size() && (int)pairs.size() < 30; i++) {
            double cx_i = (sol[i].bay_min_x + sol[i].bay_max_x) / 2.0;
            double cy_i = (sol[i].bay_min_y + sol[i].bay_max_y) / 2.0;
            double best_dist = 1e18;
            int best_j = -1;
            for (int j = i+1; j < (int)sol.size(); j++) {
                double cx_j = (sol[j].bay_min_x + sol[j].bay_max_x) / 2.0;
                double cy_j = (sol[j].bay_min_y + sol[j].bay_max_y) / 2.0;
                double d = (cx_i-cx_j)*(cx_i-cx_j) + (cy_i-cy_j)*(cy_i-cy_j);
                if (d < best_dist) { best_dist = d; best_j = j; }
            }
            if (best_j >= 0) pairs.push_back({i, best_j});
        }
        for (auto [i, j] : pairs) {
            if (i >= (int)sol.size() || j >= (int)sol.size()) continue;
            vector<PlacedBay> trial = sol;
            // Remove higher index first to preserve lower index.
            int hi = max(i,j), lo = min(i,j);
            trial.erase(trial.begin() + hi);
            trial.erase(trial.begin() + lo);
            fill_pass(trial, types, warehouse, obstacles, ceiling, wh_area);
            double Q_trial = quality(trial, wh_area);
            if (Q_trial < Q_full - EPS) {
                sol = trial;
                Q_full = Q_trial;
                full_sums = compute_sums(sol);
                improved = true;
            }
        }
    } else { // k == 3
        // Remove the 3 worst-ΔQ bays (first 3 in order).
        if ((int)order.size() < 3) return false;
        vector<int> to_remove = {order[0], order[1], order[2]};
        sort(to_remove.rbegin(), to_remove.rend()); // descending to erase safely
        vector<PlacedBay> trial = sol;
        for (int idx : to_remove) trial.erase(trial.begin() + idx);
        fill_pass(trial, types, warehouse, obstacles, ceiling, wh_area);
        double Q_trial = quality(trial, wh_area);
        if (Q_trial < Q_full - EPS) {
            sol = trial;
            improved = true;
        }
    }
    return improved;
}
```

- [ ] **Step 4: Add `improve()`.**

```cpp
// Deadline-driven improvement loop.
// Runs upgrade → fill → remove_1 → remove_2 → remove_3 until deadline or convergence.
void improve(
    vector<PlacedBay>& sol,
    Clock::time_point t_start,
    double budget_seconds,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area
) {
    while (seconds_since(t_start) < budget_seconds) {
        bool any = false;
        any |= upgrade_pass(sol, types, warehouse, obstacles, ceiling, wh_area);
        if (seconds_since(t_start) >= budget_seconds) break;
        any |= fill_pass(sol, types, warehouse, obstacles, ceiling, wh_area);
        if (seconds_since(t_start) >= budget_seconds) break;
        any |= remove_k_refill(sol, 1, types, warehouse, obstacles, ceiling, wh_area);
        if (seconds_since(t_start) >= budget_seconds) break;
        // Skip k=2 and k=3 if < 10% of budget remains.
        if (seconds_since(t_start) < budget_seconds * 0.9) {
            any |= remove_k_refill(sol, 2, types, warehouse, obstacles, ceiling, wh_area);
        }
        if (seconds_since(t_start) >= budget_seconds) break;
        if (seconds_since(t_start) < budget_seconds * 0.9) {
            any |= remove_k_refill(sol, 3, types, warehouse, obstacles, ceiling, wh_area);
        }
        if (!any) break; // converged
    }
}
```

- [ ] **Step 5: Compile.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp
```

Expected: clean compile.

- [ ] **Step 6: Commit.**

```bash
cd /Users/david.morais/HackUPC26
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): upgrade_pass + fill_pass + remove_k_refill + improve() loop"
```

---

## Task 5: Wire improve() into solve_case() and update main()

**Files:**
- Modify: `solver/solver_heuristic.cpp` — replace `solve_case()` and `main()`

- [ ] **Step 1: Replace `solve_case()` to accept `budget_per_case` and call `improve()`.**

Find the current `void solve_case(const string& case_dir)` (around line 1173) and replace the entire function with:

```cpp
void solve_case(const string& case_dir, double budget_per_case) {
    auto t0 = Clock::now();
    cout << "\n=== Solving " << case_dir << " ===\n";
    auto warehouse = read_warehouse(case_dir + "/warehouse.csv");
    auto obstacles = read_obstacles(case_dir + "/obstacles.csv");
    auto ceiling   = read_ceiling(case_dir + "/ceiling.csv");
    auto types     = read_bays(case_dir + "/types_of_bays.csv");
    ensure_ccw(warehouse);
    double wh_area = available_warehouse_area(warehouse, obstacles);
    auto variants = all_variants();
    int N = (int)variants.size();
    vector<vector<PlacedBay>> sols(N);
    vector<double> qs(N, 1e100);

    #pragma omp parallel for schedule(dynamic,1)
    for (int i = 0; i < N; i++) {
        auto t_v = Clock::now();
        sols[i] = construct_variant(variants[i], types, warehouse, obstacles, ceiling, wh_area);
        improve(sols[i], t_v, budget_per_case, types, warehouse, obstacles, ceiling, wh_area);
        auto [a,l,p,q] = details(sols[i], wh_area);
        qs[i] = q;
    }

    int best = 0;
    for (int i = 1; i < N; i++) if (qs[i] < qs[best]) best = i;

    if (!is_valid_solution(sols[best], warehouse, obstacles, ceiling)) {
        cerr << "WARNING: best solution for " << case_dir << " failed validity; emitting empty.\n";
        sols[best].clear();
        qs[best] = 1e100;
    }

    for (int i = 0; i < N; i++)
        cout << "  " << variants[i].name << " bays=" << sols[i].size()
             << " Q=" << qs[i] << (i==best?" <-- BEST":"") << "\n";

    string out_path = case_dir + "/solution.csv";
    ofstream out(out_path);
    out << "Id,X,Y,Rotation\n";
    for (auto& p : sols[best])
        out << p.id << "," << llround(p.x) << "," << llround(p.y) << ","
            << (p.angle==0?0:1) << "\n";
    out.close();
    cout << "BEST " << case_dir << " -> " << variants[best].name
         << " Q=" << qs[best] << " bays=" << sols[best].size()
         << " [" << seconds_since(t0) << "s]\n";
}
```

- [ ] **Step 2: Replace `main()` to count existing cases and compute `budget_per_case`.**

Find the current `int main()` (around line 1219) and replace it with:

```cpp
int main() {
    auto t0 = Clock::now();

    // Count how many cases actually exist on disk.
    int n_existing = 0;
    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");
        if (f.good()) n_existing++;
    }
    double budget_per_case = TOTAL_BUDGET_SECONDS / max(1, n_existing);
    cout << "cases_found=" << n_existing
         << " budget_per_case=" << budget_per_case << "s\n";

    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");
        if (f.good()) solve_case(c, budget_per_case);
        else cout << "Skipping " << c << "\n";
    }
    cout << "\n[time] total elapsed=" << seconds_since(t0) << "s\n";
    return 0;
}
```

- [ ] **Step 3: Compile.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp
```

Expected: clean compile.

- [ ] **Step 4: Run full benchmark and verify Q improvement and time budget.**

```bash
cd /Users/david.morais/HackUPC26/solver
/tmp/solver_heuristic 2>&1 | tee /tmp/heuristic_improved.log
grep "BEST\|total elapsed\|budget_per_case\|cases_found" /tmp/heuristic_improved.log
```

Check:
1. Q values for Case0–Case3 are **strictly lower** than current baseline (3242/5011/6949/11335). If any case is worse, report as DONE_WITH_CONCERNS.
2. `total elapsed` < 28s (under the 30s judge limit).
3. `cases_found` matches the number of case directories present.
4. All solutions pass validity (no WARNING lines in output).

Expected output format:
```
cases_found=N budget_per_case=Xs
BEST Case0 -> vXX_... Q=YYYY bays=ZZZ [Ws]
BEST Case1 -> vXX_... Q=YYYY bays=ZZZ [Ws]
BEST Case2 -> vXX_... Q=YYYY bays=ZZZ [Ws]
BEST Case3 -> vXX_... Q=YYYY bays=ZZZ [Ws]
[time] total elapsed=Xs
```

- [ ] **Step 5: Verify solution CSV validity for Case0–Case3.**

```bash
for c in Case0 Case1 Case2 Case3; do
  echo -n "$c: "; wc -l /Users/david.morais/HackUPC26/solver/$c/solution.csv
done
```

Each must have >= 2 lines (header + at least 1 bay).

- [ ] **Step 6: Commit with Q comparison in message.**

```bash
cd /Users/david.morais/HackUPC26
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): wire improve() into solve_case + deadline budget in main

Q results (heuristic improved vs previous heuristic vs solver.cpp baseline):
  Case0: IMPROVED / 3242 / 1533
  Case1: IMPROVED / 5011 / 2250
  Case2: IMPROVED / 6949 / 5394
  Case3: IMPROVED / 11335 / 3074
[fill in actual IMPROVED values from the run above]"
```

---

## Self-Review

**Spec coverage:**
- §5a interior grid: Task 2 — `interior_grid_points()` + integration into `construct_variant`. ✓
- §5b lookahead-1: Task 3 — `count_neighbors()` + top-K selection in construction loop. ✓
- §5c relaxed Q filter: Task 3 — `use_relax` flag + `N_relax` iterations. ✓
- §6a upgrade_pass: Task 4 step 1. ✓
- §6b fill_pass: Task 4 step 2. ✓
- §6c remove_k_refill (k=1,2,3): Task 4 step 3. ✓
- §6d improve() loop: Task 4 step 4. ✓
- §7 constants: Task 1. ✓
- §8 solve_case + main: Task 5. ✓
- §9 testing: Task 5 step 4 + step 5. ✓
- §11 risks (skip k=2,k=3 if <10% budget): Task 4 step 4 `improve()`. ✓

**Placeholder scan:** No TBD/TODO. The `[fill in actual IMPROVED values]` in Task 5 step 6 is intentional — those are runtime numbers.

**Type consistency:**
- `improve()` signature: `(vector<PlacedBay>&, Clock::time_point, double, const vector<BayType>&, const vector<Point>&, const vector<Obstacle>&, const vector<pair<double,double>>&, double)` — matches call in `solve_case()`. ✓
- `fill_pass()` uses `Variant fill_v{SweepOrder::LOWEST_LEFT, ...}` — `Variant` struct has `string name` as last field, included. ✓
- `remove_k_refill()` calls `fill_pass()` and `quality()` — both defined. ✓
- `count_neighbors()` calls `corners_from_placed_bay()`, `initial_corner_points()`, `make_candidate_axis_aligned()`, `valid_candidate()`, `SpatialIndex::build()` — all defined before this function in the file. ✓
- `solve_case()` signature changed from `(const string&)` to `(const string&, double)` — call site in `main()` updated. ✓
