# Heuristic Solver Improvements Design

**Date:** 2026-04-26
**Builds on:** `docs/superpowers/specs/2026-04-26-heuristic-solver-design.md`
**Goal:** Close the Q gap between `solver_heuristic.cpp` (purely constructive, Q~11335 on Case3) and `solver.cpp` (hill-climbing baseline, Q~3074 on Case3) by adding construction improvements and a deadline-driven post-construction improvement loop.

## 1. Current state

`solver_heuristic.cpp` (1228 lines) implements:
- 12 deterministic greedy variants run in parallel (OpenMP)
- Corner-point candidate generation
- Multi-criteria score (ratio, fill, gap, corner, ceiling)
- Hard Q-improvement filter

**Q results (minimize):**

| Case | Heuristic (current) | solver.cpp baseline |
|------|--------------------|--------------------|
| Case0 | 3242 | 1533 |
| Case1 | 5011 | 2250 |
| Case2 | 6949 | 5394 |
| Case3 | 11335 | 3074 |

All solutions are valid. Wall time: ~2.2s total (all 15 cases).

## 2. Objective

Reach Q values competitive with `solver.cpp` baseline while keeping total wall time < 30s (judge limit). Target: < 25s actual runtime (5s safety margin).

## 3. Architecture

Two improvements applied in sequence per variant:

```
construct_variant(v)           // Phase 1: improved greedy construction
   │  + interior grid points
   │  + lookahead-1 scoring
   │  + relaxed Q filter (first N_relax iterations)
   ▼
improve(sol, t_start, budget)  // Phase 2: deadline-driven local search
   │  upgrade_pass
   │  fill_pass
   │  remove_k_refill(k=1)
   │  remove_k_refill(k=2)
   │  remove_k_refill(k=3)
   │  repeat until deadline or no improvement
   ▼
return sol
```

All 12 variants run in parallel (OpenMP). Each gets the full `budget_per_case` seconds (since they run concurrently). Final answer = `argmin Q` across variants.

**Modified file:** `solver/solver_heuristic.cpp` only. No other files changed.

## 4. Time budget

```cpp
const double TOTAL_BUDGET_SECONDS = 25.0;
// budget_per_case = TOTAL_BUDGET_SECONDS / n_existing_cases
// n_existing_cases counted in main() before solving
// variant_budget = budget_per_case (variants run in parallel, so wall time = budget_per_case)
```

`n_existing_cases` is computed at runtime by checking which `CASES` dirs have `warehouse.csv`. This makes the per-case budget self-adjusting (fewer cases → more time per case).

## 5. Phase 1: Construction improvements

### 5a — Interior grid points

```
interior_grid_points(warehouse, obstacles, types) → vector<CornerPoint>

step = min over all types of min(w, d) / 2
      clamped to [500mm, 5000mm]    // avoid too-fine or too-coarse grids

for x in [bbox.min_x .. bbox.max_x] step step:
    for y in [bbox.min_y .. bbox.max_y] step step:
        if point_inside_or_on_polygon({x,y}, warehouse)
           AND not strictly inside any obstacle:
            add CornerPoint{x, y, INTERIOR_GRID}

cap at INTERIOR_GRID_CAP = 300 points (first 300 in raster order x-major)
```

These points are added to `corners` at the start of `construct_variant`, before the first iteration. They do not get regenerated per iteration (only corner-from-bay points do).

### 5b — Lookahead-1

For each valid candidate `(cp, type, rot)` that passes the Q filter, compute a base score. Take the top `LOOKAHEAD_K = 10` candidates by base score. For those K candidates, compute the lookahead term:

```
count_neighbors(sol ∪ {cand}, cp, radius, types, warehouse, obstacles, ceiling)
  → count how many distinct (type, rot) placements would be valid
    at any corner point within radius = 2 * max_bay_dim of cand
  → return count (integer)

S_lookahead = count / max_neighbors_seen_so_far (normalized, updated per iteration)
```

Final score for top-K candidates:
```
score_with_lookahead = base_score + W_LOOKAHEAD * S_lookahead
```

Candidates outside top-K use `base_score` only. This keeps complexity at `O(K × radius_check)` per iteration instead of `O(candidates²)`.

**Constants:**
```cpp
const int    LOOKAHEAD_K   = 10;
const double W_LOOKAHEAD   = 0.3;
```

### 5c — Relaxed Q filter (early iterations)

```cpp
const double RELAX_FACTOR = 0.05;
const int    N_RELAX       = max(5, (int)types.size());

// In the Q filter:
bool passes_q_filter;
if (iteration < N_RELAX) {
    passes_q_filter = (Q_new <= Q_now * (1.0 + RELAX_FACTOR));
} else {
    passes_q_filter = (Q_new < Q_now - EPS);  // original strict filter
}
```

`iteration` is the count of bays placed so far in this variant's construction loop.

## 6. Phase 2: Post-construction improvement operators

### 6a — `upgrade_pass(sol, types, warehouse, obstacles, ceiling, wh_area) → bool`

```
improved = false
for i in 0..sol.size()-1:
    best_q = Q(sol)
    best_bay = sol[i]
    for each type t in types:
        for rot in {0, 1}:
            cand = make_candidate_axis_aligned(t, sol[i].x, sol[i].y, rot)
            if valid_candidate(cand, sol, ..., ignore=i):
                q_new = Q(sol with sol[i] replaced by cand)
                if q_new < best_q - EPS:
                    best_q = q_new
                    best_bay = cand
    if best_bay != sol[i]:
        sol[i] = best_bay
        improved = true
return improved
```

Traverses bays in index order (deterministic). Uses `ignore=i` in `valid_candidate` to allow replacing a bay at its own position.

### 6b — `fill_pass(sol, types, warehouse, obstacles, ceiling, wh_area) → bool`

Rebuild corner points from current `sol` (using `initial_corner_points` + `corners_from_placed_bay` for all bays in sol), then run the standard greedy construction loop (with the strict Q filter, no relaxation) until no more bays can be added. Appends new bays to `sol`.

Returns `true` if at least one bay was added.

Uses a fixed variant for the fill: `SweepOrder::LOWEST_LEFT, TypePrio::BY_PRICE_PER_LOAD, seed=BL` — the best-performing variant from the construction phase (variant 1). No lookahead during fill (too expensive for repeated calls).

### 6c — `remove_k_refill(sol, k, types, warehouse, obstacles, ceiling, wh_area) → bool`

**k=1:** For each bay `i`, remove it, run `fill_pass` on the remainder, check if Q improves. If yes, keep the new solution. If no improvement for any `i`, return `false`.

Iterate `i` from worst-ΔQ bay to best (sort by individual bay's contribution to Q ascending — bays that contribute least are removed first, as they're most likely to free space for better placements).

**k=2:** Build a list of candidate pairs using spatial proximity: for each bay `i`, find its nearest neighbor `j` (by bounding box distance). Try removing `(i, j)`, run `fill_pass`, check Q. Limit to at most `min(n, 30)` pairs to bound runtime.

**k=3:** Remove the 3 bays with the worst individual ΔQ (same ordering as k=1 — no combinatorial search). Run `fill_pass` on remainder. Check Q.

### 6d — `improve(sol, t_start, budget, types, warehouse, obstacles, ceiling, wh_area)`

```
deadline = t_start + budget
while seconds_since(t_start) < budget:
    improved = false
    improved |= upgrade_pass(sol, ...)
    improved |= fill_pass(sol, ...)
    improved |= remove_k_refill(sol, 1, ...)
    improved |= remove_k_refill(sol, 2, ...)
    improved |= remove_k_refill(sol, 3, ...)
    if !improved: break    // converged
    if seconds_since(t_start) >= budget: break
```

The `if !improved: break` ensures the loop terminates even with generous budgets.

## 7. Constants summary

```cpp
const double TOTAL_BUDGET_SECONDS = 25.0;
const int    LOOKAHEAD_K          = 10;
const int    INTERIOR_GRID_CAP    = 300;
const double RELAX_FACTOR         = 0.05;
const double W_LOOKAHEAD          = 0.3;
// N_RELAX computed per-case: max(5, (int)types.size())
```

## 8. Updated `solve_case` and `main`

```cpp
// main(): count existing cases first
int n_existing = 0;
for (auto& c : CASES) {
    ifstream f(c + "/warehouse.csv");
    if (f.good()) n_existing++;
}
double budget_per_case = TOTAL_BUDGET_SECONDS / max(1, n_existing);

// solve_case(): pass budget to improve()
auto t_case_start = Clock::now();
...
#pragma omp parallel for schedule(dynamic, 1)
for (int i = 0; i < N; i++) {
    auto t_v = Clock::now();
    sols[i] = construct_variant(variants[i], ...);
    improve(sols[i], t_v, budget_per_case, ...);
    qs[i] = quality(sols[i], wh_area);
}
```

## 9. Testing plan

1. **Correctness:** after all improvements, call `is_valid_solution` on best solution (already present in current code). Warn + emit empty if invalid.
2. **Q comparison:** run on Case0–Case3, compare against current heuristic and `solver.cpp` baseline. Target: Q < baseline for at least 2 of 4 cases, or within 2× on all 4.
3. **Wall time:** total elapsed < 25s for all existing cases.
4. **Determinism:** run twice, solution CSVs must be byte-identical (may differ between machines due to timing in improvement loop — acceptable).

## 10. Out of scope

- Angles other than 0°/90°.
- Changes to `solver.cpp`.
- Adding new variants beyond the existing 12.
- Tuning weights per-case.

## 11. Risks

- **Lookahead cost:** if `count_neighbors` is slow on large warehouses with many candidates, it could make construction too slow. Mitigation: only compute for top-K=10 candidates, and cap the neighborhood radius search using `SpatialIndex`.
- **remove_k=2 pairs:** worst case O(n×30) fill_pass calls per loop iteration — each fill_pass is O(candidates×types×2). If n is large (>200 bays) this can be slow. Mitigation: deadline check before each remove_k call; if < 10% of budget remains, skip k=2 and k=3.
- **Relaxed filter early on:** may produce initial placements that degrade Q so much that the improvement loop can't recover. Mitigation: `RELAX_FACTOR=0.05` is conservative (only 5% worse allowed).
