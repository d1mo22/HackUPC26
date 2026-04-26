# Local-Fill remove_k_refill Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `fill_pass(trial, ...)` inside `remove_k_refill` with a `local_fill_pass` that only scans corner points near the vacated region, making `remove_k_refill` fast enough to run within the 25s budget.

**Architecture:** Add `local_fill_pass(sol, removed_bays, radius, ...)` that generates candidate corners only from the removed bays' own corners + bays within `radius` of the removal center, then runs the same greedy fill loop restricted to those points. Also add a `top_k_filter` pre-pass for `k=1` that limits candidates to the best 30% by removal-ΔQ. Re-enable `remove_k_refill` in `improve()`.

**Tech Stack:** C++17, OpenMP (libomp via Homebrew on macOS).

**Baseline Q (current, minimize):**
- Case0: 1682, Case1: 4362, Case2: 4838, Case3: 11080

**Compile command:**
```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp
```

**Run command (from solver/ dir):**
```bash
cd /Users/david.morais/HackUPC26/solver && /tmp/solver_heuristic 2>&1 | grep "BEST\|total elapsed\|WARNING"
```

**File to modify:** `solver/solver_heuristic.cpp` only.

---

## Key code locations (current file)

- Line 32: improvement constants block
- Line 1277: `// ============== IMPROVEMENT OPERATORS ==============`
- Line 1277–1393: `upgrade_pass()` + `fill_pass()`
- Line 1395–1487: `remove_k_refill()` — currently calls `fill_pass` (too slow)
- Line 1489–1508: `improve()` — currently has `remove_k_refill` disabled
- Line 1510: `solve_case()`

## Supporting API (already defined in file, do not redefine)

```cpp
// Existing types
struct PlacedBay { int id; double x,y; int w,d,h,gap; double angle,price,loads; 
                   double bay_min_x,bay_max_x,bay_min_y,bay_max_y; ... };
struct BayType    { int id,w,d,h,gap,loads,price; };
struct QSums      { double price,loads,area; };
struct Obstacle   { double x,y,w,d; };
struct CornerPoint { double x,y; enum Origin { ..., INTERIOR_GRID } origin; };

// Existing functions used by remove_k_refill
QSums   compute_sums(const vector<PlacedBay>&);
double  quality_from_sums(QSums, double wh_area, bool empty);
double  quality(const vector<PlacedBay>&, double wh_area);
double  quality_with_added(QSums, double price, double loads, double bay_area, double wh_area);
vector<CornerPoint> corners_from_placed_bay(const PlacedBay&);
vector<CornerPoint> initial_corner_points(const vector<Point>& warehouse);
void    filter_corners_inside_envelope(vector<CornerPoint>&, const PlacedBay&);
PlacedBay make_candidate_axis_aligned(const BayType&, double x, double y, int rot);
bool    valid_candidate(double x,double y,int w,int d,int h,int gap,double angle,
                        const vector<PlacedBay>& sol, const vector<Point>& wh,
                        const vector<Obstacle>&, const vector<pair<double,double>>& ceil,
                        int ignore=-1, SpatialIndex* idx=nullptr);
double  score_candidate(const PlacedBay&, const Variant&, const ScoreNorms&,
                        const vector<PlacedBay>&, const vector<Point>&,
                        const vector<Obstacle>&, const vector<pair<double,double>>&);
ScoreNorms compute_norms(const vector<BayType>&, const vector<pair<double,double>>&);
vector<int> sort_types(const vector<BayType>&, TypePrio);
void    sort_corners(vector<CornerPoint>&, const Variant&, const vector<Point>&);
bool    fill_pass(vector<PlacedBay>&, const vector<BayType>&, const vector<Point>&,
                  const vector<Obstacle>&, const vector<pair<double,double>>&, double wh_area);

// Constants
const double EPS;
const double TOTAL_BUDGET_SECONDS;
const int    INTERIOR_GRID_CAP;
const double RELAX_FACTOR;
```

---

## Task 1: Add `LOCAL_FILL_RADIUS_FACTOR` constant

**Files:**
- Modify: `solver/solver_heuristic.cpp` (lines 32–37, constants block)

- [ ] **Step 1: Add constant after existing improvement constants.**

Find:
```cpp
const double W_LOOKAHEAD          = 0.3;
```

Replace with:
```cpp
const double W_LOOKAHEAD          = 0.3;
const int    REMOVE_TOP_K_FRAC    = 3;   // try top 1/REMOVE_TOP_K_FRAC bays by removal-ΔQ
const double LOCAL_FILL_RADIUS    = 3.0; // multiplier of max_bay_dim for local fill region
```

- [ ] **Step 2: Compile to confirm no errors.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp 2>&1
```

Expected: clean compile (no errors).

- [ ] **Step 3: Commit.**

```bash
cd /Users/david.morais/HackUPC26
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): add LOCAL_FILL_RADIUS + REMOVE_TOP_K_FRAC constants"
```

---

## Task 2: Add `local_fill_pass()`

**Files:**
- Modify: `solver/solver_heuristic.cpp` — insert `local_fill_pass()` immediately before `remove_k_refill()` (currently around line 1395)

- [ ] **Step 1: Insert `local_fill_pass()` before `bool remove_k_refill(`.**

Find:
```cpp
bool remove_k_refill(
    vector<PlacedBay>& sol,
    int k,
```

Insert immediately before it:
```cpp
// Fast fill restricted to the local region around the removed bays.
// Generates corner points only from: (a) corners of each removed bay,
// (b) corners of bays in sol whose center is within `radius` of any removed bay center.
// Runs greedy fill (strict Q filter, no lookahead) until no improvement found.
// Returns true if at least one bay was added.
bool local_fill_pass(
    vector<PlacedBay>& sol,
    const vector<PlacedBay>& removed_bays,
    double radius,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area
) {
    if (types.empty() || removed_bays.empty()) return false;

    ScoreNorms norms = compute_norms(types, ceiling);
    Variant fill_v{SweepOrder::LOWEST_LEFT, TypePrio::BY_PRICE_PER_LOAD, 0,
                   1.0, 0.5, 0.3, 0.3, 0.2, "local_fill"};
    vector<int> type_order = sort_types(types, fill_v.type_prio);

    // Collect centers of removed bays for proximity test.
    vector<pair<double,double>> removed_centers;
    for (auto& rb : removed_bays)
        removed_centers.push_back({(rb.bay_min_x+rb.bay_max_x)/2.0,
                                    (rb.bay_min_y+rb.bay_max_y)/2.0});

    auto near_removal = [&](double cx, double cy) -> bool {
        for (auto [rx, ry] : removed_centers) {
            double dx = cx-rx, dy = cy-ry;
            if (dx*dx+dy*dy <= radius*radius) return true;
        }
        return false;
    };

    // Seed corners from removed bays themselves.
    vector<CornerPoint> corners;
    for (auto& rb : removed_bays) {
        auto c = corners_from_placed_bay(rb);
        for (auto& cp : c) corners.push_back(cp);
    }
    // Add corners from bays in sol that are close to the removal.
    for (auto& p : sol) {
        double cx = (p.bay_min_x+p.bay_max_x)/2.0;
        double cy = (p.bay_min_y+p.bay_max_y)/2.0;
        if (!near_removal(cx, cy)) continue;
        auto c = corners_from_placed_bay(p);
        for (auto& cp : c) corners.push_back(cp);
    }
    // Also include initial warehouse corners (handles edge/wall placements).
    auto wh_corners = initial_corner_points(warehouse);
    for (auto& wc : wh_corners) corners.push_back(wc);

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
            // Only consider corners inside the local region.
            if (!near_removal(cp.x, cp.y)) continue;
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
        // Add corners from the newly placed bay.
        auto nc = corners_from_placed_bay(best.bay);
        for (auto& c : nc) corners.push_back(c);
        filter_corners_inside_envelope(corners, best.bay);
        any_added = true;
    }
    return any_added;
}

```

- [ ] **Step 2: Compile.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp 2>&1
```

Expected: clean compile.

- [ ] **Step 3: Commit.**

```bash
cd /Users/david.morais/HackUPC26
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): add local_fill_pass() for vacated-region refill"
```

---

## Task 3: Rewrite `remove_k_refill()` to use `local_fill_pass`

**Files:**
- Modify: `solver/solver_heuristic.cpp` — replace body of `remove_k_refill()` (currently lines 1395–1487)

The current function signature and body starts at `bool remove_k_refill(`. Replace the entire function with the version below, which:
- For k=1: only tries the top `n / REMOVE_TOP_K_FRAC` bays (best removal-ΔQ candidates), uses `local_fill_pass` instead of `fill_pass`
- For k=2: same spatial-pair approach but with `local_fill_pass`
- For k=3: same top-3 approach but with `local_fill_pass`

- [ ] **Step 1: Replace `remove_k_refill()` body.**

Find the entire current function:
```cpp
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
                order.resize(sol.size());
                iota(order.begin(), order.end(), 0);
                sort(order.begin(), order.end(), [&](int a, int b){
                    return q_without(a) < q_without(b);
                });
            }
        }
    } else if (k == 2) {
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
        if ((int)order.size() < 3) return false;
        vector<int> to_remove = {order[0], order[1], order[2]};
        sort(to_remove.rbegin(), to_remove.rend());
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

Replace with:
```cpp
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

    // Compute local-fill radius: LOCAL_FILL_RADIUS × max bay dimension.
    int max_bay_dim = 1;
    for (auto& t : types) max_bay_dim = max(max_bay_dim, max(t.w + t.gap, t.d + t.gap));
    double local_radius = LOCAL_FILL_RADIUS * max_bay_dim;

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

    // Sort by removal-ΔQ ascending (removing these bays hurts Q least).
    vector<int> order(sol.size());
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b){
        return q_without(a) < q_without(b);
    });

    if (k == 1) {
        // Only try top 1/REMOVE_TOP_K_FRAC of bays.
        int n_try = max(1, (int)order.size() / REMOVE_TOP_K_FRAC);
        for (int ii = 0; ii < n_try; ii++) {
            int idx = order[ii];
            vector<PlacedBay> removed = {sol[idx]};
            vector<PlacedBay> trial = sol;
            trial.erase(trial.begin() + idx);
            local_fill_pass(trial, removed, local_radius, types, warehouse, obstacles, ceiling, wh_area);
            double Q_trial = quality(trial, wh_area);
            if (Q_trial < Q_full - EPS) {
                sol = trial;
                full_sums = compute_sums(sol);
                Q_full = Q_trial;
                improved = true;
                // Recompute order after change.
                order.resize(sol.size());
                iota(order.begin(), order.end(), 0);
                sort(order.begin(), order.end(), [&](int a, int b){
                    return q_without(a) < q_without(b);
                });
                n_try = max(1, (int)order.size() / REMOVE_TOP_K_FRAC);
                ii = -1; // restart loop
            }
        }
    } else if (k == 2) {
        // Spatially adjacent pairs (cap 30 pairs), use local fill.
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
            int hi = max(i,j), lo = min(i,j);
            vector<PlacedBay> removed = {sol[hi], sol[lo]};
            vector<PlacedBay> trial = sol;
            trial.erase(trial.begin() + hi);
            trial.erase(trial.begin() + lo);
            local_fill_pass(trial, removed, local_radius, types, warehouse, obstacles, ceiling, wh_area);
            double Q_trial = quality(trial, wh_area);
            if (Q_trial < Q_full - EPS) {
                sol = trial;
                Q_full = Q_trial;
                full_sums = compute_sums(sol);
                improved = true;
            }
        }
    } else { // k == 3
        if ((int)order.size() < 3) return false;
        vector<int> to_remove = {order[0], order[1], order[2]};
        sort(to_remove.rbegin(), to_remove.rend());
        vector<PlacedBay> removed;
        for (int idx : to_remove) removed.push_back(sol[idx]);
        vector<PlacedBay> trial = sol;
        for (int idx : to_remove) trial.erase(trial.begin() + idx);
        local_fill_pass(trial, removed, local_radius, types, warehouse, obstacles, ceiling, wh_area);
        double Q_trial = quality(trial, wh_area);
        if (Q_trial < Q_full - EPS) {
            sol = trial;
            improved = true;
        }
    }
    return improved;
}
```

- [ ] **Step 2: Compile.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp 2>&1
```

Expected: clean compile.

- [ ] **Step 3: Commit.**

```bash
cd /Users/david.morais/HackUPC26
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): use local_fill_pass in remove_k_refill (top-k filter + local region)"
```

---

## Task 4: Re-enable `remove_k_refill` in `improve()`

**Files:**
- Modify: `solver/solver_heuristic.cpp` — update `improve()` body (currently lines 1489–1508)

- [ ] **Step 1: Replace the `improve()` body to re-enable `remove_k_refill`.**

Find:
```cpp
    while (seconds_since(t_start) < budget_seconds) {
        bool any = false;
        any |= upgrade_pass(sol, types, warehouse, obstacles, ceiling, wh_area);
        if (seconds_since(t_start) >= budget_seconds) break;
        any |= fill_pass(sol, types, warehouse, obstacles, ceiling, wh_area);
        if (seconds_since(t_start) >= budget_seconds) break;
        // remove_k_refill disabled for now — too slow for typical case sizes
        if (!any) break;
    }
```

Replace with:
```cpp
    while (seconds_since(t_start) < budget_seconds) {
        bool any = false;
        any |= upgrade_pass(sol, types, warehouse, obstacles, ceiling, wh_area);
        if (seconds_since(t_start) >= budget_seconds) break;
        any |= fill_pass(sol, types, warehouse, obstacles, ceiling, wh_area);
        if (seconds_since(t_start) >= budget_seconds) break;
        any |= remove_k_refill(sol, 1, types, warehouse, obstacles, ceiling, wh_area);
        if (seconds_since(t_start) >= budget_seconds) break;
        if (seconds_since(t_start) < budget_seconds * 0.9) {
            any |= remove_k_refill(sol, 2, types, warehouse, obstacles, ceiling, wh_area);
        }
        if (seconds_since(t_start) >= budget_seconds) break;
        if (seconds_since(t_start) < budget_seconds * 0.9) {
            any |= remove_k_refill(sol, 3, types, warehouse, obstacles, ceiling, wh_area);
        }
        if (!any) break;
    }
```

- [ ] **Step 2: Compile.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic \
    /Users/david.morais/HackUPC26/solver/solver_heuristic.cpp 2>&1
```

Expected: clean compile.

- [ ] **Step 3: Run quick timing test — must finish in < 30s.**

```bash
cd /Users/david.morais/HackUPC26/solver
time /tmp/solver_heuristic 2>&1 | grep "BEST\|total elapsed\|WARNING"
```

If `total elapsed` > 28s: the `remove_k_refill` is still too slow. In that case, reduce `REMOVE_TOP_K_FRAC` from 3 to 5 (fewer candidates tried) or reduce `LOCAL_FILL_RADIUS` from 3.0 to 2.0.

If it finishes in time: check Q values against baseline (1682/4362/4838/11080). They must be equal or lower.

- [ ] **Step 4: Commit.**

```bash
cd /Users/david.morais/HackUPC26
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): re-enable remove_k_refill with local fill in improve() loop

Q results (new vs previous heuristic vs solver.cpp baseline):
  Case0: [NEW] / 1682 / 1533
  Case1: [NEW] / 4362 / 2250
  Case2: [NEW] / 4838 / 5394
  Case3: [NEW] / 11080 / 3074
[fill in NEW values from run above]"
```

---

## Task 5: Tune constants if timing or quality is off

This task is conditional on the results from Task 4.

**Files:**
- Modify: `solver/solver_heuristic.cpp` (lines 32–39, constants block)

- [ ] **Step 1: If `total elapsed` > 28s, reduce candidates.**

Find:
```cpp
const int    REMOVE_TOP_K_FRAC    = 3;
const double LOCAL_FILL_RADIUS    = 3.0;
```

Replace with:
```cpp
const int    REMOVE_TOP_K_FRAC    = 5;   // try top 1/5 bays
const double LOCAL_FILL_RADIUS    = 2.5;
```

Recompile and re-run. If still > 28s, reduce further to `REMOVE_TOP_K_FRAC = 8` and `LOCAL_FILL_RADIUS = 2.0`.

- [ ] **Step 2: If Q is not improving beyond current values, increase radius.**

If Q results are same as baseline (no improvement from remove_k_refill), try:
```cpp
const double LOCAL_FILL_RADIUS    = 4.0;
```

Recompile and re-run.

- [ ] **Step 3: Commit final tuned constants (if changed).**

```bash
cd /Users/david.morais/HackUPC26
git add solver/solver_heuristic.cpp
git commit -m "tune(heuristic): adjust LOCAL_FILL_RADIUS and REMOVE_TOP_K_FRAC for timing/quality"
```

---

## Self-Review

**Spec coverage:**
- Local fill function added: Task 2 ✓
- replace `fill_pass` in `remove_k_refill` with `local_fill_pass`: Task 3 ✓
- Top-k filter for k=1: Task 3 (n_try = n/REMOVE_TOP_K_FRAC) ✓
- Re-enable in `improve()`: Task 4 ✓
- Timing guard: Task 4 step 3 (fallback tuning in Task 5) ✓

**Placeholder scan:** No TBD/TODO. "[fill in NEW values]" in Task 4 step 4 is intentional runtime data.

**Type consistency:**
- `local_fill_pass` signature: `(vector<PlacedBay>&, const vector<PlacedBay>&, double, ...)` — matches all 3 call sites in Task 3. ✓
- `removed` is built as `vector<PlacedBay>` before calls. ✓
- `LOCAL_FILL_RADIUS` (double) × `max_bay_dim` (int) → double. ✓
- `REMOVE_TOP_K_FRAC` used as `order.size() / REMOVE_TOP_K_FRAC` — integer division, fine. ✓
