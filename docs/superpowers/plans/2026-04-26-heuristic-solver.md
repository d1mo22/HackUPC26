# Heuristic Solver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a fully deterministic, purely heuristic constructive solver (`solver/solver_heuristic.cpp`) for the Mecalux warehouse bay placement problem, run on Case0–Case3 (and angled cases as smoke tests), and compare Q against the existing `solver.cpp` baseline.

**Architecture:** Single C++17 binary. Multi-pass deterministic construction: build 12 variants (each = a tuple of sweep order, type priority, seed corner, and score weights), construct each greedily by scoring corner-point candidates with a multi-criteria score, return the variant with the best (lowest) Q. Reuses geometry primitives copied verbatim from `solver/solver.cpp` (no header refactor — YAGNI per spec §2). Parallelism via OpenMP across the 12 variants.

**Tech Stack:** C++17, OpenMP (libomp via Homebrew on macOS), no external libraries beyond STL.

**Spec:** `docs/superpowers/specs/2026-04-26-heuristic-solver-design.md`

**Existing assets we reuse (copied from `solver/solver.cpp`):**
- `Point`, `BayType`, `PlacedBay`, `Obstacle`, `Bounds`, `Trig` structs (lines 52–82).
- Geometry helpers: `polygon_area` (157), `polygon_inside_polygon` (238), `polygons_overlap_area` (319), `make_rotated_rect` (341), `gap_poly_values` (366), `polygon_bounds` (263), `bb_disjoint_v` (683), `obstacle_poly` (421), `minmax_x` (430).
- I/O: `read_warehouse` (495), `read_obstacles` (512), `read_ceiling` (532), `read_bays` (551), `split_csv_line` (444), `try_stod`/`try_stoi` (483/489).
- `min_ceiling_between` (572).
- `SpatialIndex` (599) and `valid_candidate` (689) — but called only with `angle ∈ {0, 90}`.
- `quality` (786), `QSums` (805), `compute_sums` (807), `quality_with_added` (824).
- `obstacles_area` (843), `available_warehouse_area` (853), `details` (860).
- `is_valid_solution` (1577) for the verifier.

**What we DO NOT copy or call:** `hill`, `hill_sa`, `add_bay`, `replace_bay`, `fill_aggressive`, `remove_k_and_refill`, `upgrade_bay`, `shared_gap_refill`, `rotate_compact_and_add`, `add_45_degree_bay`, `compact_diagonal_bays_on_axes`, `position_perturb_and_add`, `build_initial`, `build_active_ops`, `OperatorStats`, all `_SA` variants, anything involving angles other than 0/90, any RNG.

---

## File Structure

- **Create:** `solver/solver_heuristic.cpp` — the entire heuristic solver (estimated 700–900 lines).
- **No modifications** to existing files. The plan is intentionally additive so we can A/B compare with `solver.cpp`.
- **Build artifact:** `/tmp/solver_heuristic` (matches the convention of `/tmp/solver` from CLAUDE.md).
- **Outputs:** `solver/Case{N}/solution.csv` — same format as `solver.cpp` (`Id,X,Y,Rotation`).

The new file is self-contained on purpose (spec §2: "copied, not refactored to header — YAGNI"). When the heuristic proves out, a follow-up plan can extract shared geometry into a header.

---

## Task 1: Skeleton, I/O, and "empty solution" smoke test

**Files:**
- Create: `solver/solver_heuristic.cpp`

- [ ] **Step 1: Create the file with the minimal skeleton — copied geometry + I/O + a `main` that reads each case and prints area info, but writes no bays yet.**

```cpp
// solver/solver_heuristic.cpp
//
// Deterministic heuristic constructive solver for the Mecalux warehouse
// bay placement problem. See:
//   docs/superpowers/specs/2026-04-26-heuristic-solver-design.md
//
// Build (macOS + Homebrew libomp):
//   g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include \
//       -Xpreprocessor -fopenmp -lomp \
//       -L/opt/homebrew/opt/libomp/lib \
//       -o /tmp/solver_heuristic solver/solver_heuristic.cpp
//
// Run:
//   cd solver && /tmp/solver_heuristic

#include <bits/stdc++.h>
#include <chrono>
#ifdef _OPENMP
#include <omp.h>
#endif
using namespace std;
using Clock = chrono::steady_clock;

// ============== CONFIG ==============

const vector<string> CASES = {
    "Case0", "Case1", "Case2", "Case3",
    "CaseWeird", "Case40",
    "CaseAngledA", "CaseAngledB", "CaseAngledC", "CaseAngledD",
    "CaseDiagArmA", "CaseDiagArmB", "CaseDiagArmC", "CaseDiagArmD",
    "CaseForcedAngle"
};

const double EPS = 1e-7;

// ============== TYPES ==============
// (Copied verbatim from solver/solver.cpp lines 52-82.)

struct Point { double x, y; };

struct BayType {
    int id, w, d, h, gap, loads;
    double price;
};

struct PlacedBay {
    int id;
    double x, y;
    int w, d, h, gap;
    int loads;
    double price;
    int angle;                 // 0 or 90 only in this solver
    vector<Point> bay_pts;
    vector<Point> gap_pts;
    double bay_min_x, bay_min_y, bay_max_x, bay_max_y;
    double gap_min_x, gap_min_y, gap_max_x, gap_max_y;
};

struct Obstacle { double x, y, w, d; };

struct Bounds { double min_x, min_y, max_x, max_y; };

// ============== GEOMETRY ==============
// Copied verbatim from solver/solver.cpp. The functions we need:
//   polygon_area, polygon_inside_polygon, polygons_overlap_area,
//   make_rotated_rect, gap_poly_values, polygon_bounds, bb_disjoint_v,
//   obstacle_poly, minmax_x, point_inside_or_on_polygon, cross, dotp,
//   polygons_overlap_area helpers (sat_overlap_positive_area, project_poly),
//   trig_for_angle, refresh_cache.
//
// COPY-VERBATIM-MARKER: we will copy lines 149-442 of solver/solver.cpp
// into this section, and the SpatialIndex (599-682) plus valid_candidate
// (689-782) into a later section.

// [TASK 1 STEP 2 INSERTS THE COPIED CODE HERE]

// ============== I/O ==============
// Copied verbatim from solver/solver.cpp lines 444-571 (split_csv_line,
// file_empty_or_missing, try_stod, try_stoi, read_warehouse,
// read_obstacles, read_ceiling, read_bays).

// [TASK 1 STEP 2 INSERTS THE COPIED CODE HERE]

// ============== CEILING ==============
// Copied verbatim from solver/solver.cpp line 572 (min_ceiling_between).

// [TASK 1 STEP 2 INSERTS THE COPIED CODE HERE]

// ============== AREA / Q HELPERS ==============
// Copied verbatim from solver/solver.cpp:
//   obstacles_area (843), available_warehouse_area (853),
//   QSums (805), compute_sums (807), quality_from_sums (817),
//   quality_with_added (824), quality (786), details (860).

// [TASK 1 STEP 2 INSERTS THE COPIED CODE HERE]

// ============== MAIN (skeleton) ==============

double seconds_since(Clock::time_point t0) {
    return chrono::duration<double>(Clock::now() - t0).count();
}

void solve_case_skeleton(const string& case_dir) {
    auto t0 = Clock::now();
    cout << "\n=== Solving " << case_dir << " ===\n";

    auto warehouse = read_warehouse(case_dir + "/warehouse.csv");
    auto obstacles = read_obstacles(case_dir + "/obstacles.csv");
    auto ceiling   = read_ceiling(case_dir + "/ceiling.csv");
    auto types     = read_bays(case_dir + "/types_of_bays.csv");

    double total_wh_area = polygon_area(warehouse);
    double obs_area      = obstacles_area(obstacles);
    double wh_area       = available_warehouse_area(warehouse, obstacles);

    cout << "polygon_pts=" << warehouse.size()
         << " obstacles=" << obstacles.size()
         << " ceiling_segments=" << ceiling.size()
         << " types=" << types.size()
         << " area_total=" << total_wh_area
         << " obstacles_area=" << obs_area
         << " available_area=" << wh_area
         << "\n";

    // No bays placed yet — write empty solution.csv to confirm I/O path works.
    string out_path = case_dir + "/solution.csv";
    ofstream out(out_path);
    out << "Id,X,Y,Rotation\n";
    out.close();

    cout << "[time] " << case_dir << " elapsed=" << seconds_since(t0) << "s\n";
}

int main() {
    auto t0 = Clock::now();
    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");
        if (f.good()) solve_case_skeleton(c);
        else cout << "Skipping " << c << "\n";
    }
    cout << "\n[time] total elapsed=" << seconds_since(t0) << "s\n";
    return 0;
}
```

- [ ] **Step 2: Copy the marked geometry/I/O/Q-helpers blocks verbatim from `solver/solver.cpp` into the placeholders above.**

You will copy four contiguous blocks from `solver/solver.cpp` and paste them where the `[TASK 1 STEP 2 INSERTS THE COPIED CODE HERE]` markers are. Do NOT modify the copied code — copy verbatim.

| Block | Source range in `solver/solver.cpp` | Paste under |
|---|---|---|
| Geometry (cross, dotp, polygon_area, point_on_segment, orient, proper_segment_intersection, point_inside_or_on_polygon, any_proper_segment_crossing, polygon_inside_polygon, project_poly, polygon_bounds, bounds_disjoint, sat_overlap_positive_area, polygons_overlap_area, trig_for_angle, make_rotated_rect, gap_poly_values, bay_poly, gap_poly, refresh_cache, obstacle_poly, minmax_x) | Lines **149–442** | `// ============== GEOMETRY ==============` |
| I/O parsers (split_csv_line, file_empty_or_missing, try_stod, try_stoi, read_warehouse, read_obstacles, read_ceiling, read_bays) | Lines **444–571** | `// ============== I/O ==============` |
| Ceiling (min_ceiling_between) | Line **572–597** | `// ============== CEILING ==============` |
| Area / Q helpers (quality, QSums, compute_sums, quality_from_sums, quality_with_added, used_area, obstacles_area, available_warehouse_area, details) | Lines **786–873** | `// ============== AREA / Q HELPERS ==============` |

If a copied function references a global like `Trig` or `PI`, also copy any constants needed. Specifically: copy `const double PI = acos(-1.0);` from line 37, and the `Trig` struct (line 79) and `trig_for_angle` from line 323. Place `PI` and `Trig` near the top of the GEOMETRY section.

You will also need `bb_disjoint_v` (line 683). Copy it at the end of the GEOMETRY section.

After copying, the file should compile.

- [ ] **Step 3: Compile.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic solver/solver_heuristic.cpp
```

Expected: clean compile (warnings about unused functions are OK).

If you get linker errors about `_omp_*`, retry without OpenMP for now (we don't use it yet):

```bash
g++ -O3 -std=c++17 -o /tmp/solver_heuristic solver/solver_heuristic.cpp
```

If unused functions cause errors (they shouldn't — copied code is self-contained), do NOT remove them; instead, mark with `[[maybe_unused]]` or leave alone.

- [ ] **Step 4: Run skeleton smoke test.**

```bash
cd solver && /tmp/solver_heuristic
```

Expected output for each case:
- A line `=== Solving CaseN ===` followed by a `polygon_pts=...` line with non-zero `area_total` and `available_area`.
- An `[time] CaseN elapsed=...s` line (should be < 0.1s per case).
- `solution.csv` created in each case dir with only the header row.

Verify:
```bash
head -1 solver/Case0/solution.csv
# Expected: Id,X,Y,Rotation
wc -l solver/Case0/solution.csv
# Expected: 1 (header only)
```

- [ ] **Step 5: Commit.**

```bash
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): scaffold solver_heuristic.cpp with geometry/IO copied from solver.cpp"
```

---

## Task 2: CornerPoint generation + valid_candidate adapter (one bay placed by hand)

**Files:**
- Modify: `solver/solver_heuristic.cpp`

We add the corner-point generator and a small adapter around `valid_candidate`. To prove they work end-to-end, we hard-code placing a single bay (the type with the best price/loads ratio) at the bottom-left corner of each warehouse's bounding box (or the next valid corner if BL is invalid). This is throwaway — Task 4 replaces it.

- [ ] **Step 1: Add `SpatialIndex` and `valid_candidate` (copied verbatim from `solver/solver.cpp` lines 599–782).**

Paste both into `solver_heuristic.cpp` directly above `// ============== AREA / Q HELPERS ==============`. Add a section header:

```cpp
// ============== SPATIAL INDEX + VALIDITY ==============
// Copied verbatim from solver/solver.cpp lines 599-782.
//
// Note: valid_candidate accepts arbitrary integer angles, but in this
// solver we will only ever pass angle ∈ {0, 90}.
```

- [ ] **Step 2: Add `CornerPoint`, sweep ordering, and the corner-point generator.**

Place after the SPATIAL INDEX section. Code:

```cpp
// ============== CORNER POINTS ==============

struct CornerPoint {
    double x, y;
    enum Origin {
        WAREHOUSE_BBOX,    // 4 corners of polygon bounding box
        CONCAVE_VERTEX,    // concave vertex of warehouse polygon
        BAY_TL, BAY_TR, BAY_BL, BAY_BR,
        GAP_TL, GAP_TR, GAP_BL, GAP_BR
    } origin;
};

// Returns true if vertex i of polygon p is concave assuming CCW winding.
// Uses cross product of incoming and outgoing edges.
static bool is_concave_vertex_ccw(const vector<Point>& p, int i) {
    int n = (int)p.size();
    Point a = p[(i - 1 + n) % n];
    Point b = p[i];
    Point c = p[(i + 1) % n];
    // For CCW polygon, convex vertices have cross > 0, concave have cross < 0.
    return cross(a, b, c) < -EPS;
}

// Ensures polygon vertices are in CCW order (positive signed area).
// polygon_area returns absolute area, so we use the signed shoelace formula.
static void ensure_ccw(vector<Point>& poly) {
    double s = 0.0;
    int n = (int)poly.size();
    for (int i = 0; i < n; i++) {
        const Point& a = poly[i];
        const Point& b = poly[(i + 1) % n];
        s += (b.x - a.x) * (b.y + a.y);
    }
    // Shoelace sign convention: s > 0 means CW, s < 0 means CCW. Flip if CW.
    if (s > 0) reverse(poly.begin(), poly.end());
}

// Initial candidates before any bay is placed.
vector<CornerPoint> initial_corner_points(const vector<Point>& warehouse) {
    vector<CornerPoint> out;
    Bounds bb = polygon_bounds(warehouse);
    out.push_back({bb.min_x, bb.min_y, CornerPoint::WAREHOUSE_BBOX});
    out.push_back({bb.max_x, bb.min_y, CornerPoint::WAREHOUSE_BBOX});
    out.push_back({bb.min_x, bb.max_y, CornerPoint::WAREHOUSE_BBOX});
    out.push_back({bb.max_x, bb.max_y, CornerPoint::WAREHOUSE_BBOX});
    int n = (int)warehouse.size();
    for (int i = 0; i < n; i++) {
        if (is_concave_vertex_ccw(warehouse, i)) {
            out.push_back({warehouse[i].x, warehouse[i].y, CornerPoint::CONCAVE_VERTEX});
        }
    }
    return out;
}

// 8 new corners introduced by placing a bay (4 from bay rect, 4 from
// bay+gap rect). Bay must be axis-aligned (angle 0 or 90).
vector<CornerPoint> corners_from_placed_bay(const PlacedBay& p) {
    vector<CornerPoint> out;
    out.push_back({p.bay_min_x, p.bay_min_y, CornerPoint::BAY_BL});
    out.push_back({p.bay_max_x, p.bay_min_y, CornerPoint::BAY_BR});
    out.push_back({p.bay_min_x, p.bay_max_y, CornerPoint::BAY_TL});
    out.push_back({p.bay_max_x, p.bay_max_y, CornerPoint::BAY_TR});
    if (p.gap > 0) {
        out.push_back({p.gap_min_x, p.gap_min_y, CornerPoint::GAP_BL});
        out.push_back({p.gap_max_x, p.gap_min_y, CornerPoint::GAP_BR});
        out.push_back({p.gap_min_x, p.gap_max_y, CornerPoint::GAP_TL});
        out.push_back({p.gap_max_x, p.gap_max_y, CornerPoint::GAP_TR});
    }
    return out;
}

// Removes corner points strictly inside (not on boundary of) a placed bay's
// bay+gap envelope, since they cannot host a future bay.
void filter_corners_inside_envelope(vector<CornerPoint>& corners, const PlacedBay& p) {
    double envx0 = min(p.bay_min_x, p.gap_min_x);
    double envy0 = min(p.bay_min_y, p.gap_min_y);
    double envx1 = max(p.bay_max_x, p.gap_max_x);
    double envy1 = max(p.bay_max_y, p.gap_max_y);
    if (p.gap == 0) {
        envx0 = p.bay_min_x; envy0 = p.bay_min_y;
        envx1 = p.bay_max_x; envy1 = p.bay_max_y;
    }
    auto strictly_inside = [&](const CornerPoint& c) {
        return c.x > envx0 + EPS && c.x < envx1 - EPS
            && c.y > envy0 + EPS && c.y < envy1 - EPS;
    };
    corners.erase(
        remove_if(corners.begin(), corners.end(), strictly_inside),
        corners.end());
}
```

- [ ] **Step 3: Add a `make_candidate_axis_aligned` helper that builds a `PlacedBay` from `(x, y, type, rotation)` with rotation in {0, 1}.**

Place right after the corner-point block:

```cpp
// rotation: 0 -> use (w, d) as-is; 1 -> swap, equivalent to angle 90.
PlacedBay make_candidate_axis_aligned(const BayType& t, double x, double y, int rotation) {
    PlacedBay p{};
    p.id = t.id;
    p.x = x; p.y = y;
    p.w = t.w; p.d = t.d; p.h = t.h; p.gap = t.gap;
    p.loads = t.loads; p.price = t.price;
    p.angle = (rotation == 0) ? 0 : 90;
    refresh_cache(p);   // populates bay_pts, gap_pts, and bb extents
    return p;
}
```

- [ ] **Step 4: Replace `solve_case_skeleton` with a "place one bay" version that proves the pipeline works.**

Replace the body of `solve_case_skeleton` (rename to `solve_case_one_bay`) with:

```cpp
void solve_case_one_bay(const string& case_dir) {
    auto t0 = Clock::now();
    cout << "\n=== Solving " << case_dir << " ===\n";

    auto warehouse = read_warehouse(case_dir + "/warehouse.csv");
    auto obstacles = read_obstacles(case_dir + "/obstacles.csv");
    auto ceiling   = read_ceiling(case_dir + "/ceiling.csv");
    auto types     = read_bays(case_dir + "/types_of_bays.csv");

    ensure_ccw(warehouse);

    double wh_area = available_warehouse_area(warehouse, obstacles);
    if (types.empty()) {
        cout << "no types, skip\n";
        return;
    }

    // Pick the type with the best (lowest) price/loads ratio.
    int best_type = 0;
    double best_ratio = types[0].price / max(1, types[0].loads);
    for (int i = 1; i < (int)types.size(); i++) {
        double r = types[i].price / max(1, types[i].loads);
        if (r < best_ratio) { best_ratio = r; best_type = i; }
    }

    auto corners = initial_corner_points(warehouse);
    vector<PlacedBay> sol;

    for (auto& cp : corners) {
        for (int rot = 0; rot < 2; rot++) {
            PlacedBay cand = make_candidate_axis_aligned(types[best_type], cp.x, cp.y, rot);
            if (valid_candidate(cand.x, cand.y, cand.w, cand.d, cand.h, cand.gap,
                                cand.angle, sol, warehouse, obstacles, ceiling)) {
                sol.push_back(cand);
                goto placed;
            }
        }
    }
placed:

    string out_path = case_dir + "/solution.csv";
    ofstream out(out_path);
    out << "Id,X,Y,Rotation\n";
    for (auto& p : sol) {
        out << p.id << "," << llround(p.x) << "," << llround(p.y) << ","
            << (p.angle == 0 ? 0 : 1) << "\n";
    }
    out.close();

    auto [a, l, pr, q] = details(sol, wh_area);
    cout << "bays=" << sol.size() << " area=" << a << " loads=" << l
         << " price=" << pr << " Q=" << q
         << " [" << seconds_since(t0) << "s]\n";
}

int main() {
    auto t0 = Clock::now();
    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");
        if (f.good()) solve_case_one_bay(c);
        else cout << "Skipping " << c << "\n";
    }
    cout << "\n[time] total elapsed=" << seconds_since(t0) << "s\n";
    return 0;
}
```

- [ ] **Step 5: Compile and run.**

```bash
g++ -O3 -std=c++17 -o /tmp/solver_heuristic solver/solver_heuristic.cpp
cd solver && /tmp/solver_heuristic
```

Expected: each Case0–Case3 prints `bays=1` (the lone hand-placed bay) and a finite `Q`. `solution.csv` has exactly 2 lines (header + one row).

Verify Case0:
```bash
wc -l solver/Case0/solution.csv  # Expected: 2
```

- [ ] **Step 6: Commit.**

```bash
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): corner-point generator + valid_candidate adapter, place 1 bay smoke test"
```

---

## Task 3: Multi-criteria score function

**Files:**
- Modify: `solver/solver_heuristic.cpp`

Implement the score components from spec §5. Score the candidate placement; this task does NOT yet implement the construction loop — only the scoring building blocks, which we exercise with a unit-style test in `main`.

- [ ] **Step 1: Add the `Variant` struct and the score function.**

Insert after the corner-point block:

```cpp
// ============== VARIANT + SCORE ==============

enum class SweepOrder { LOWEST_LEFT, LEFTMOST_LOW, LONGEST_EDGE_FIRST };
enum class TypePrio   { BY_PRICE_PER_LOAD, BY_AREA_DESC, BY_AREA_ASC, BY_DENSITY };

struct Variant {
    SweepOrder sweep;
    TypePrio   type_prio;
    int        seed_corner;     // 0=BL, 1=BR, 2=TL, 3=TR
    double w_ratio, w_fill, w_gap_pair, w_corner, w_ceiling;
    string name;                // for logging
};

// Globals computed once per case for normalization.
struct ScoreNorms {
    double max_price_per_load;  // max price/loads across all types
    double max_bay_area;        // max w*d across all types
    double max_ceiling;         // max ceiling height observed
};

ScoreNorms compute_norms(const vector<BayType>& types,
                         const vector<pair<double, double>>& ceiling) {
    ScoreNorms n{0.0, 0.0, 0.0};
    for (auto& t : types) {
        double pr = t.price / max(1, t.loads);
        if (pr > n.max_price_per_load) n.max_price_per_load = pr;
        double a = (double)t.w * (double)t.d;
        if (a > n.max_bay_area) n.max_bay_area = a;
    }
    for (auto& s : ceiling) if (s.second > n.max_ceiling) n.max_ceiling = s.second;
    if (n.max_price_per_load <= 0) n.max_price_per_load = 1.0;
    if (n.max_bay_area      <= 0) n.max_bay_area      = 1.0;
    if (n.max_ceiling       <= 0) n.max_ceiling       = 1.0;
    return n;
}

// Returns the number of distinct sides of the candidate bay's rectangle that
// touch an existing edge (warehouse boundary, obstacle edge, or another bay's
// edge). Result in {0, 1, 2, 3, 4}; saturates >=2 to "corner".
int count_touching_sides(const PlacedBay& cand,
                         const vector<PlacedBay>& sol,
                         const vector<Point>& warehouse,
                         const vector<Obstacle>& obstacles) {
    auto on_boundary = [&](double v, double a, double b) {
        return fabs(v - a) < EPS || fabs(v - b) < EPS;
    };
    int touches = 0;
    Bounds wb = polygon_bounds(warehouse);
    if (on_boundary(cand.bay_min_x, wb.min_x, wb.max_x)) touches++;
    if (on_boundary(cand.bay_max_x, wb.min_x, wb.max_x)) touches++;
    if (on_boundary(cand.bay_min_y, wb.min_y, wb.max_y)) touches++;
    if (on_boundary(cand.bay_max_y, wb.min_y, wb.max_y)) touches++;
    for (auto& o : obstacles) {
        double ox0 = o.x, oy0 = o.y, ox1 = o.x + o.w, oy1 = o.y + o.d;
        // touching iff one side coincides AND y/x intervals overlap
        if (fabs(cand.bay_max_x - ox0) < EPS &&
            !(cand.bay_max_y < oy0 + EPS || cand.bay_min_y > oy1 - EPS)) touches++;
        if (fabs(cand.bay_min_x - ox1) < EPS &&
            !(cand.bay_max_y < oy0 + EPS || cand.bay_min_y > oy1 - EPS)) touches++;
        if (fabs(cand.bay_max_y - oy0) < EPS &&
            !(cand.bay_max_x < ox0 + EPS || cand.bay_min_x > ox1 - EPS)) touches++;
        if (fabs(cand.bay_min_y - oy1) < EPS &&
            !(cand.bay_max_x < ox0 + EPS || cand.bay_min_x > ox1 - EPS)) touches++;
    }
    for (auto& other : sol) {
        if (fabs(cand.bay_max_x - other.bay_min_x) < EPS &&
            !(cand.bay_max_y < other.bay_min_y + EPS || cand.bay_min_y > other.bay_max_y - EPS)) touches++;
        if (fabs(cand.bay_min_x - other.bay_max_x) < EPS &&
            !(cand.bay_max_y < other.bay_min_y + EPS || cand.bay_min_y > other.bay_max_y - EPS)) touches++;
        if (fabs(cand.bay_max_y - other.bay_min_y) < EPS &&
            !(cand.bay_max_x < other.bay_min_x + EPS || cand.bay_min_x > other.bay_max_x - EPS)) touches++;
        if (fabs(cand.bay_min_y - other.bay_max_y) < EPS &&
            !(cand.bay_max_x < other.bay_min_x + EPS || cand.bay_min_x > other.bay_max_x - EPS)) touches++;
    }
    return touches;
}

// Detects face-to-face gap sharing: cand has a gap on one side and an
// existing bay's gap or wall is co-linear and overlapping on that side.
// Returns true if the candidate participates in at least one shared gap.
bool has_shared_gap(const PlacedBay& cand,
                    const vector<PlacedBay>& sol,
                    const vector<Point>& warehouse) {
    if (cand.gap == 0) return false;
    // Determine which side carries the gap. From gap_poly_values:
    //   angle 0  -> gap is on the +y side of bay (top)
    //   angle 90 -> gap is on the +x side of bay (right)
    if (cand.angle == 0) {
        // gap occupies y in [bay_max_y, bay_max_y + gap]
        Bounds wb = polygon_bounds(warehouse);
        // wall sharing: top boundary of warehouse equals top of gap
        if (fabs(cand.gap_max_y - wb.max_y) < EPS) return true;
        for (auto& o : sol) {
            // check if other has its gap on -y side (i.e., other.angle == 0
            // and other is above us, with overlapping x-range)
            if (o.angle == 0 && o.gap > 0 &&
                fabs(o.bay_min_y - cand.gap_max_y) < EPS &&
                !(o.bay_max_x < cand.bay_min_x + EPS ||
                  o.bay_min_x > cand.bay_max_x - EPS)) {
                return true;
            }
            // bay-to-bay direct contact along gap face: rare but counts
        }
    } else {  // angle == 90, gap on +x side
        Bounds wb = polygon_bounds(warehouse);
        if (fabs(cand.gap_max_x - wb.max_x) < EPS) return true;
        for (auto& o : sol) {
            if (o.angle == 90 && o.gap > 0 &&
                fabs(o.bay_min_x - cand.gap_max_x) < EPS &&
                !(o.bay_max_y < cand.bay_min_y + EPS ||
                  o.bay_min_y > cand.bay_max_y - EPS)) {
                return true;
            }
        }
    }
    return false;
}

// Multi-criteria score for placing `cand` next to current `sol`.
// Range: roughly [0, sum_of_weights].
double score_candidate(const PlacedBay& cand,
                       const Variant& v,
                       const ScoreNorms& norms,
                       const vector<PlacedBay>& sol,
                       const vector<Point>& warehouse,
                       const vector<Obstacle>& obstacles,
                       const vector<pair<double, double>>& ceiling) {
    double S_ratio = (cand.price / max(1, cand.loads)) / norms.max_price_per_load;
    double bay_area = (double)cand.w * (double)cand.d;
    double S_fill   = bay_area / norms.max_bay_area;
    double S_gap    = has_shared_gap(cand, sol, warehouse) ? 1.0 : 0.0;
    int touches = count_touching_sides(cand, sol, warehouse, obstacles);
    double S_corner = (touches >= 2) ? 1.0 : (touches == 1 ? 0.5 : 0.0);

    double local_ceiling = min_ceiling_between(cand.bay_min_x, cand.bay_max_x, ceiling);
    double S_ceil = 1.0;
    if (norms.max_ceiling > EPS) {
        double slack = local_ceiling - cand.h;
        if (slack < 0) slack = 0;
        S_ceil = 1.0 - slack / norms.max_ceiling;
        if (S_ceil < 0) S_ceil = 0;
        if (S_ceil > 1) S_ceil = 1;
    }

    return v.w_ratio    * (1.0 - S_ratio)   // lower price/load is better, hence (1 - S)
         + v.w_fill     * S_fill
         + v.w_gap_pair * S_gap
         + v.w_corner   * S_corner
         + v.w_ceiling  * S_ceil;
}
```

**Why `1.0 - S_ratio`:** lower price/loads is better (it lowers Q). The raw `S_ratio` gives high values to *expensive* bays per load. We invert so the score component rewards cheap-per-load bays.

- [ ] **Step 2: Add a quick `main` exercise that prints the score of placing the cheapest type at the bottom-left corner of each case.**

Replace `solve_case_one_bay` body's final block (after `placed:` label) with this addition before writing the CSV:

```cpp
    if (!sol.empty()) {
        Variant probe{
            SweepOrder::LOWEST_LEFT, TypePrio::BY_PRICE_PER_LOAD, 0,
            1.0, 0.5, 0.3, 0.3, 0.2, "probe"
        };
        ScoreNorms norms = compute_norms(types, ceiling);
        // Recompute the score against an empty solution to isolate it.
        vector<PlacedBay> empty_sol;
        double s = score_candidate(sol[0], probe, norms, empty_sol,
                                   warehouse, obstacles, ceiling);
        cout << "score(first_bay)=" << s
             << " touches=" << count_touching_sides(sol[0], empty_sol, warehouse, obstacles)
             << " shared_gap=" << has_shared_gap(sol[0], empty_sol, warehouse)
             << "\n";
    }
```

- [ ] **Step 3: Compile and run.**

```bash
g++ -O3 -std=c++17 -o /tmp/solver_heuristic solver/solver_heuristic.cpp
cd solver && /tmp/solver_heuristic
```

Expected: each non-empty case now also prints a `score(first_bay)=...` line. The score should be a non-negative finite number, and `touches >= 1` (since the bay sits at a bbox corner of the warehouse, it touches at least one wall — usually two).

- [ ] **Step 4: Commit.**

```bash
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): multi-criteria score (ratio/fill/gap/corner/ceiling)"
```

---

## Task 4: Construction loop (single variant)

**Files:**
- Modify: `solver/solver_heuristic.cpp`

Implement the per-variant constructive loop from spec §6.

- [ ] **Step 1: Add sweep ordering helpers.**

Insert after the score block:

```cpp
// ============== SWEEP ORDERING ==============

// Returns the longest edge of the warehouse polygon and a point that should
// serve as the sweep origin (one endpoint of that edge). Used by
// LONGEST_EDGE_FIRST.
struct EdgeSeed { Point origin; double dx, dy; double length; };
EdgeSeed longest_edge_seed(const vector<Point>& warehouse) {
    int n = (int)warehouse.size();
    EdgeSeed best{ {0,0}, 1, 0, 0 };
    for (int i = 0; i < n; i++) {
        Point a = warehouse[i], b = warehouse[(i + 1) % n];
        double dx = b.x - a.x, dy = b.y - a.y;
        double L = sqrt(dx * dx + dy * dy);
        if (L > best.length) {
            // Pick the endpoint with smaller (x+y) as origin.
            Point o = (a.x + a.y <= b.x + b.y) ? a : b;
            best = {o, dx, dy, L};
        }
    }
    return best;
}

void sort_corners(vector<CornerPoint>& cps,
                  const Variant& v,
                  const vector<Point>& warehouse) {
    if (v.sweep == SweepOrder::LOWEST_LEFT) {
        // seed_corner flips axes: 0=BL (asc, asc), 1=BR (asc, desc x),
        // 2=TL (desc y, asc x), 3=TR (desc, desc).
        bool flip_x = (v.seed_corner == 1 || v.seed_corner == 3);
        bool flip_y = (v.seed_corner == 2 || v.seed_corner == 3);
        sort(cps.begin(), cps.end(), [&](const CornerPoint& a, const CornerPoint& b) {
            double ya = flip_y ? -a.y : a.y;
            double yb = flip_y ? -b.y : b.y;
            if (fabs(ya - yb) > EPS) return ya < yb;
            double xa = flip_x ? -a.x : a.x;
            double xb = flip_x ? -b.x : b.x;
            return xa < xb;
        });
    } else if (v.sweep == SweepOrder::LEFTMOST_LOW) {
        bool flip_x = (v.seed_corner == 1 || v.seed_corner == 3);
        bool flip_y = (v.seed_corner == 2 || v.seed_corner == 3);
        sort(cps.begin(), cps.end(), [&](const CornerPoint& a, const CornerPoint& b) {
            double xa = flip_x ? -a.x : a.x;
            double xb = flip_x ? -b.x : b.x;
            if (fabs(xa - xb) > EPS) return xa < xb;
            double ya = flip_y ? -a.y : a.y;
            double yb = flip_y ? -b.y : b.y;
            return ya < yb;
        });
    } else { // LONGEST_EDGE_FIRST
        EdgeSeed s = longest_edge_seed(warehouse);
        // Sort by projection along the edge direction, then perpendicular.
        double L = max(s.length, 1.0);
        double tx = s.dx / L, ty = s.dy / L;
        double nx = -ty, ny = tx;
        sort(cps.begin(), cps.end(), [&](const CornerPoint& a, const CornerPoint& b) {
            double ta = (a.x - s.origin.x) * tx + (a.y - s.origin.y) * ty;
            double tb = (b.x - s.origin.x) * tx + (b.y - s.origin.y) * ty;
            if (fabs(ta - tb) > EPS) return ta < tb;
            double na = (a.x - s.origin.x) * nx + (a.y - s.origin.y) * ny;
            double nb = (b.x - s.origin.x) * nx + (b.y - s.origin.y) * ny;
            return na < nb;
        });
    }
}

vector<int> sort_types(const vector<BayType>& types, TypePrio p) {
    vector<int> idx(types.size());
    iota(idx.begin(), idx.end(), 0);
    auto pr = [&](int i) { return types[i].price / max(1, types[i].loads); };
    auto ar = [&](int i) { return (double)types[i].w * (double)types[i].d; };
    auto den = [&](int i) { return pr(i) / max(1.0, ar(i)); };
    sort(idx.begin(), idx.end(), [&](int a, int b) {
        switch (p) {
            case TypePrio::BY_PRICE_PER_LOAD: return pr(a) < pr(b);
            case TypePrio::BY_AREA_DESC:      return ar(a) > ar(b);
            case TypePrio::BY_AREA_ASC:       return ar(a) < ar(b);
            case TypePrio::BY_DENSITY:        return den(a) < den(b);
        }
        return false;
    });
    return idx;
}
```

- [ ] **Step 2: Add the construction loop.**

```cpp
// ============== CONSTRUCTION LOOP ==============

vector<PlacedBay> construct_variant(
    const Variant& v,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area
) {
    ScoreNorms norms = compute_norms(types, ceiling);
    vector<int> type_order = sort_types(types, v.type_prio);
    vector<PlacedBay> sol;
    vector<CornerPoint> corners = initial_corner_points(warehouse);

    // Use SpatialIndex from solver.cpp; cell sized by largest bay dimension.
    int max_dim = 1;
    for (auto& t : types) max_dim = max(max_dim, max(t.w + t.gap, t.d + t.gap));
    SpatialIndex idx;
    idx.cell_size = max_dim > 0 ? max_dim : 1;

    while (true) {
        sort_corners(corners, v, warehouse);

        QSums sums = compute_sums(sol);
        double Q_now = sol.empty() ? 1e100 : quality_from_sums(sums, wh_area, false);

        struct Best { double score; int cp_idx; int type_id_idx; int rot; PlacedBay bay; };
        Best best{ -1e100, -1, -1, -1, {} };

        for (int ci = 0; ci < (int)corners.size(); ci++) {
            const CornerPoint& cp = corners[ci];
            for (int ti : type_order) {
                const BayType& t = types[ti];
                for (int rot = 0; rot < 2; rot++) {
                    PlacedBay cand = make_candidate_axis_aligned(t, cp.x, cp.y, rot);
                    if (!valid_candidate(cand.x, cand.y, cand.w, cand.d, cand.h, cand.gap,
                                         cand.angle, sol, warehouse, obstacles, ceiling, -1, &idx))
                        continue;
                    double bay_area = (double)cand.w * (double)cand.d;
                    double Q_new = quality_with_added(sums, cand.price, cand.loads, bay_area, wh_area);
                    // Hard filter: only accept if it strictly improves Q.
                    if (!sol.empty() && Q_new >= Q_now - EPS) continue;
                    double s = score_candidate(cand, v, norms, sol, warehouse, obstacles, ceiling);
                    bool win = (s > best.score + EPS);
                    if (!win && fabs(s - best.score) <= EPS) {
                        // Deterministic tie-break: smaller (x+y), then type_id, then rot.
                        if (best.cp_idx < 0) win = true;
                        else if (cp.x + cp.y < corners[best.cp_idx].x + corners[best.cp_idx].y - EPS) win = true;
                        else if (fabs(cp.x + cp.y - (corners[best.cp_idx].x + corners[best.cp_idx].y)) <= EPS) {
                            if (t.id < best.bay.id) win = true;
                            else if (t.id == best.bay.id && rot < best.rot) win = true;
                        }
                    }
                    if (win) best = { s, ci, ti, rot, cand };
                }
            }
        }

        if (best.cp_idx < 0) break;

        // Insert into spatial index. Use bay+gap envelope.
        double envx0 = best.bay.bay_min_x, envy0 = best.bay.bay_min_y;
        double envx1 = best.bay.bay_max_x, envy1 = best.bay.bay_max_y;
        if (best.bay.gap > 0) {
            envx0 = min(envx0, best.bay.gap_min_x);
            envy0 = min(envy0, best.bay.gap_min_y);
            envx1 = max(envx1, best.bay.gap_max_x);
            envy1 = max(envy1, best.bay.gap_max_y);
        }
        idx.insert((int)sol.size(), envx0, envy0, envx1, envy1);
        sol.push_back(best.bay);

        auto new_corners = corners_from_placed_bay(best.bay);
        for (auto& nc : new_corners) corners.push_back(nc);
        filter_corners_inside_envelope(corners, best.bay);
    }

    return sol;
}
```

**SpatialIndex API note:** check the actual member names in `solver.cpp` lines 599–682 for `insert(...)` and `query(...)`. If they differ from `idx.insert(int, x0, y0, x1, y1)` and `idx.query(...)`, adapt the calls. Also, if `idx.cell_size` is private, use whatever public initializer the struct exposes (e.g., constructor or `init` method). Inspect lines 599–682 before you write this code.

- [ ] **Step 3: Wire one fixed variant into `main` and replace the previous stub.**

Replace `solve_case_one_bay` and `main` with:

```cpp
void solve_case_single_variant(const string& case_dir) {
    auto t0 = Clock::now();
    cout << "\n=== Solving " << case_dir << " ===\n";

    auto warehouse = read_warehouse(case_dir + "/warehouse.csv");
    auto obstacles = read_obstacles(case_dir + "/obstacles.csv");
    auto ceiling   = read_ceiling(case_dir + "/ceiling.csv");
    auto types     = read_bays(case_dir + "/types_of_bays.csv");
    ensure_ccw(warehouse);

    double wh_area = available_warehouse_area(warehouse, obstacles);

    Variant v{ SweepOrder::LOWEST_LEFT, TypePrio::BY_PRICE_PER_LOAD, 0,
               1.0, 0.5, 0.3, 0.3, 0.2, "v1_LL_PPL_BL" };

    auto sol = construct_variant(v, types, warehouse, obstacles, ceiling, wh_area);

    string out_path = case_dir + "/solution.csv";
    ofstream out(out_path);
    out << "Id,X,Y,Rotation\n";
    for (auto& p : sol) {
        out << p.id << "," << llround(p.x) << "," << llround(p.y) << ","
            << (p.angle == 0 ? 0 : 1) << "\n";
    }
    out.close();

    auto [a, l, pr, q] = details(sol, wh_area);
    cout << "variant=" << v.name << " bays=" << sol.size()
         << " area=" << a << " loads=" << l << " price=" << pr
         << " Q=" << q << " [" << seconds_since(t0) << "s]\n";
}

int main() {
    auto t0 = Clock::now();
    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");
        if (f.good()) solve_case_single_variant(c);
        else cout << "Skipping " << c << "\n";
    }
    cout << "\n[time] total elapsed=" << seconds_since(t0) << "s\n";
    return 0;
}
```

- [ ] **Step 4: Compile and run.**

```bash
g++ -O3 -std=c++17 -o /tmp/solver_heuristic solver/solver_heuristic.cpp
cd solver && /tmp/solver_heuristic
```

Expected:
- Each Case0..Case3 now produces `bays >= 1` (typically dozens to hundreds).
- `Q` is finite and significantly lower than the empty-solution baseline (~1e100).
- Wall time per case < 5s.
- Run twice; outputs are byte-identical:
  ```bash
  cp solver/Case0/solution.csv /tmp/sol0.csv
  cd solver && /tmp/solver_heuristic && cd ..
  diff /tmp/sol0.csv solver/Case0/solution.csv
  # Expected: no diff
  ```

- [ ] **Step 5: Commit.**

```bash
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): single-variant deterministic construction loop"
```

---

## Task 5: 12 variants in parallel + argmin Q

**Files:**
- Modify: `solver/solver_heuristic.cpp`

Define the 12 variants from spec §7 and pick the one with the lowest Q.

- [ ] **Step 1: Add the variants table.**

Insert above `solve_case_single_variant`:

```cpp
// ============== VARIANTS TABLE ==============

vector<Variant> all_variants() {
    using SO = SweepOrder;
    using TP = TypePrio;
    return {
        { SO::LOWEST_LEFT,        TP::BY_PRICE_PER_LOAD, 0, 1.0, 0.5, 0.3, 0.3, 0.2, "v01_LL_PPL_BL"    },
        { SO::LOWEST_LEFT,        TP::BY_AREA_DESC,      0, 0.5, 1.0, 0.3, 0.3, 0.2, "v02_LL_AD_BL"     },
        { SO::LOWEST_LEFT,        TP::BY_AREA_ASC,       0, 0.7, 0.7, 0.5, 0.3, 0.2, "v03_LL_AA_BL"     },
        { SO::LEFTMOST_LOW,       TP::BY_PRICE_PER_LOAD, 0, 1.0, 0.5, 0.3, 0.3, 0.2, "v04_LML_PPL_BL"   },
        { SO::LEFTMOST_LOW,       TP::BY_DENSITY,        0, 0.7, 0.7, 0.3, 0.3, 0.2, "v05_LML_DEN_BL"   },
        { SO::LONGEST_EDGE_FIRST, TP::BY_PRICE_PER_LOAD, 0, 1.0, 0.5, 0.5, 0.3, 0.2, "v06_LE_PPL"       },
        { SO::LONGEST_EDGE_FIRST, TP::BY_AREA_DESC,      0, 0.5, 1.0, 0.5, 0.3, 0.2, "v07_LE_AD"        },
        { SO::LOWEST_LEFT,        TP::BY_PRICE_PER_LOAD, 1, 1.0, 0.5, 0.3, 0.3, 0.2, "v08_LL_PPL_BR"    },
        { SO::LOWEST_LEFT,        TP::BY_PRICE_PER_LOAD, 2, 1.0, 0.5, 0.3, 0.3, 0.2, "v09_LL_PPL_TL"    },
        { SO::LOWEST_LEFT,        TP::BY_PRICE_PER_LOAD, 3, 1.0, 0.5, 0.3, 0.3, 0.2, "v10_LL_PPL_TR"    },
        { SO::LOWEST_LEFT,        TP::BY_AREA_DESC,      0, 1.0, 1.0, 0.5, 0.5, 0.5, "v11_LL_AD_BL_aggr"},
        { SO::LONGEST_EDGE_FIRST, TP::BY_AREA_ASC,       0, 0.5, 0.5, 1.0, 0.5, 0.2, "v12_LE_AA_gap"    },
    };
}
```

- [ ] **Step 2: Replace `solve_case_single_variant` with `solve_case` that runs all 12 variants in parallel and picks argmin Q.**

```cpp
void solve_case(const string& case_dir) {
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

    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < N; i++) {
        sols[i] = construct_variant(variants[i], types, warehouse, obstacles, ceiling, wh_area);
        auto [a, l, p, q] = details(sols[i], wh_area);
        qs[i] = q;
    }

    int best = 0;
    for (int i = 1; i < N; i++) if (qs[i] < qs[best]) best = i;

    for (int i = 0; i < N; i++) {
        cout << "  " << variants[i].name << " bays=" << sols[i].size()
             << " Q=" << qs[i] << (i == best ? " <-- BEST" : "") << "\n";
    }

    string out_path = case_dir + "/solution.csv";
    ofstream out(out_path);
    out << "Id,X,Y,Rotation\n";
    for (auto& p : sols[best]) {
        out << p.id << "," << llround(p.x) << "," << llround(p.y) << ","
            << (p.angle == 0 ? 0 : 1) << "\n";
    }
    out.close();
    cout << "BEST " << case_dir << " -> " << variants[best].name
         << " Q=" << qs[best] << " [" << seconds_since(t0) << "s]\n";
}

int main() {
    auto t0 = Clock::now();
    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");
        if (f.good()) solve_case(c);
        else cout << "Skipping " << c << "\n";
    }
    cout << "\n[time] total elapsed=" << seconds_since(t0) << "s\n";
    return 0;
}
```

- [ ] **Step 3: Compile with OpenMP.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic solver/solver_heuristic.cpp
```

If linking fails with `library 'omp' not found`, run `brew install libomp` first, then retry.

If your environment has no libomp, fall back to a serial loop by removing the `#pragma omp parallel for` line — the result is identical, just slower.

- [ ] **Step 4: Run all cases.**

```bash
cd solver && /tmp/solver_heuristic
```

Expected:
- Per case, 12 variant lines printed, each with `bays=` and `Q=`.
- One marked `<-- BEST`.
- Wall time per case < 10s on M-series (parallel).
- Determinism check (run twice, byte-identical):
  ```bash
  /tmp/solver_heuristic > /tmp/run1.txt && /tmp/solver_heuristic > /tmp/run2.txt
  diff /tmp/run1.txt /tmp/run2.txt
  diff solver/Case0/solution.csv solver/Case0/solution.csv  # No diff after re-run
  ```
  If the two `*.txt` differ in only timing lines (`elapsed=...`), that's fine; solution CSVs must match.

- [ ] **Step 5: Commit.**

```bash
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): 12 deterministic variants in parallel + argmin Q"
```

---

## Task 6: Validity verifier and Q comparison vs `solver.cpp`

**Files:**
- Modify: `solver/solver_heuristic.cpp`

Add a post-construction validity check using `is_valid_solution` (copied from `solver.cpp`) and a comparison report.

- [ ] **Step 1: Copy `is_valid_solution` from `solver/solver.cpp` lines 1577–1606 into `solver_heuristic.cpp`.**

Place it after the AREA / Q HELPERS section, under a new heading:

```cpp
// ============== VALIDITY VERIFIER ==============
// Copied verbatim from solver/solver.cpp lines 1577-1606.
```

If `is_valid_solution` references functions or constants we already copied (e.g., `valid_candidate`, `polygon_inside_polygon`), they're available. If it references something else, copy that too — verify by compiling.

- [ ] **Step 2: Use it in `solve_case` and refuse invalid solutions.**

In `solve_case`, after picking `best`, add:

```cpp
    if (!is_valid_solution(sols[best], warehouse, obstacles, ceiling)) {
        cerr << "WARNING: best solution for " << case_dir
             << " failed is_valid_solution; emitting empty solution.\n";
        sols[best].clear();
    }
```

- [ ] **Step 3: Add a comparison-vs-baseline step.**

After writing the CSV but before the final cout in `solve_case`, also read the existing `solver.cpp`-produced `solution.csv` (if any) for comparison. Skip — keep the comparison out of the binary. Instead, document the comparison procedure in a one-liner in stdout, and we'll do the comparison manually:

Replace the final `cout << "BEST ..."` with:

```cpp
    cout << "BEST " << case_dir << " -> " << variants[best].name
         << " Q=" << qs[best] << " bays=" << sols[best].size()
         << " [" << seconds_since(t0) << "s]\n";
```

(No code change beyond what Task 5 already produced.)

- [ ] **Step 4: Compile, run, and produce the comparison table manually.**

```bash
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic solver/solver_heuristic.cpp
g++ -O3 -std=c++17 -o /tmp/solver_baseline solver/solver.cpp

# Run baseline first; its outputs to /tmp keep heuristic's outputs from being overwritten:
mkdir -p /tmp/baseline-out
cd solver && /tmp/solver_baseline 2>&1 | tee /tmp/baseline-out/run.log && cd ..
for c in Case0 Case1 Case2 Case3; do cp solver/$c/solution.csv /tmp/baseline-out/$c.solution.csv; done

# Now run heuristic
cd solver && /tmp/solver_heuristic 2>&1 | tee /tmp/heuristic.log && cd ..
```

Then build a summary table by hand from the two log files (Q values for each case) and append it to the commit message in step 5.

- [ ] **Step 5: Commit.**

```bash
git add solver/solver_heuristic.cpp
git commit -m "feat(heuristic): integrate is_valid_solution; add comparison procedure

Q comparison Case0..Case3 (heuristic vs solver.cpp baseline):
  Case0: <fill in>
  Case1: <fill in>
  Case2: <fill in>
  Case3: <fill in>"
```

(If the heuristic loses by a wide margin on a case, that's expected — this is a baseline. Tuning weights is a follow-up plan.)

---

## Task 7: README note + final smoke

**Files:**
- Modify: `solver/README.md` (only if it exists; otherwise skip)

- [ ] **Step 1: Check if `solver/README.md` exists.**

```bash
test -f solver/README.md && echo EXISTS || echo MISSING
```

If MISSING: skip steps 2–3 of this task. Do not create a new README (CLAUDE.md says: never create docs proactively).

- [ ] **Step 2: Append a short build/run note for the new binary.**

```bash
cat >> solver/README.md <<'EOF'

## Heuristic solver (`solver_heuristic.cpp`)

Deterministic, purely heuristic constructive solver. 12 parallel variants, no metaheuristic.
Spec: `docs/superpowers/specs/2026-04-26-heuristic-solver-design.md`

Build:
```
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic solver/solver_heuristic.cpp
```

Run from `solver/`:
```
cd solver && /tmp/solver_heuristic
```
EOF
```

- [ ] **Step 3: Commit.**

```bash
git add solver/README.md
git commit -m "docs(solver): note solver_heuristic.cpp build/run"
```

- [ ] **Step 4: Final end-to-end smoke.**

```bash
cd solver && /tmp/solver_heuristic
```

Verify all four `Case0..Case3/solution.csv` files exist and have ≥ 2 lines each:

```bash
for c in Case0 Case1 Case2 Case3; do
  echo -n "$c: "; wc -l solver/$c/solution.csv
done
```

Each should print at least 2 lines (header + at least one bay). If any case has only the header (1 line), it means the construction loop produced zero bays — investigate before declaring done.

---

## Self-Review

I checked the plan against the spec:

**Spec coverage:**
- §1 Problem recap, §2 Architecture: Tasks 1–5 build the architecture exactly as described.
- §3 Data structures: `Variant`, `CornerPoint`, `ScoreNorms` all defined (Tasks 2, 3).
- §4 Corner-point generation: Task 2 covers initial bbox corners + concave vertices + post-placement corners + lazy filtering. Validation reuses `valid_candidate` which already implements warehouse/obstacle/bay overlap and ceiling.
- §5 Multi-criteria score: Task 3 covers all five components (`S_ratio`, `S_fill`, `S_gap_pair`, `S_corner`, `S_ceiling`) with the documented hard filter (only accept if Q strictly improves) and tie-break.
- §6 Construction loop: Task 4.
- §7 Variants table: Task 5 reproduces all 12.
- §8 CLI/outputs: Task 1 establishes the CSV writing path; Task 5 finalizes it.
- §9 Testing plan: Task 1 verifies I/O, Task 4 verifies determinism, Task 6 verifies validity and produces the Q comparison.
- §10 Out of scope: respected — only 0°/90° rotation; no header refactor; no changes to `solver.cpp`.
- §11 Open risks: addressed — `ensure_ccw` normalizes polygon winding (risk #2); deterministic tie-break in score (risk #3); aggressive variants 11–12 mitigate the strict-improvement filter (risk #1).

**Placeholder scan:** No "TBD"/"TODO". One intentional `<fill in>` in Task 6's commit message — that's the Q numbers we measure at runtime, not a code placeholder.

**Type consistency:** `make_candidate_axis_aligned` rotation argument uses `0/1` everywhere; `PlacedBay::angle` stores `0`/`90`. The CSV emit converts via `(p.angle == 0 ? 0 : 1)` — consistent across Tasks 2, 4, 5. `score_candidate` and `count_touching_sides` signatures match where called. `SpatialIndex` API note flags an inspect-before-use to confirm member names — added explicitly in Task 4.

**One ambiguity I caught and fixed inline:** the `S_ratio` direction. Lower price/loads is better, but the raw `S_ratio` rewards higher values. The score uses `1.0 - S_ratio` to invert. Documented inline in Task 3.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-26-heuristic-solver.md`. Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** — Execute tasks in this session using `executing-plans`, batch execution with checkpoints.

Which approach?
