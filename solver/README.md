# solver/

The C++ optimisation engine for the Mecalux Warehouse Optimizer. One source file, two run modes, 29 bundled test cases.

## Build

```bash
# macOS / Linux — no OpenMP needed (uses std::async for parallelism)
g++ -O3 -std=c++17 -o /tmp/solver solver.cpp
```

The frontend bridge compiles the solver automatically on startup, so you only need the manual build for CLI use.

## Run modes

### CLI mode — solve one case directory

```bash
/tmp/solver path/to/case_dir
```

`case_dir` must contain the four input CSVs **flat** (no nested `CaseX/`):

```
case_dir/
  warehouse.csv
  obstacles.csv
  ceiling.csv
  types_of_bays.csv
```

The solver writes `case_dir/solution.csv`. This is the mode the frontend uses.

### Discovery mode — solve all `CaseX/` subfolders

```bash
cd solver && /tmp/solver
```

Scans the current directory for subfolders that contain all four CSVs, then solves each one. `solution.csv` is written into each subfolder. Folders are processed in a preferred order (see `PREFERRED_CASE_ORDER` in `solver.cpp`).

## Output

One line per case to stdout:

```
=== Solving <case_dir> ===
BEST bays=18 area=3.728e+07 loads=147 price=44100 Q=1180.87 elapsed=2.3s -> <case_dir>/solution.csv
```

`solution.csv` columns: `Id,X,Y,Rotation` (rotation in degrees, multiples of 90 in I/O).

## Algorithm

Hill climbing + simulated annealing with **10 parallel restarts** via `std::async(launch::async, ...)`. Each restart has its own seeded `thread_local mt19937` and runs until the wall-time deadline or iteration cap is reached.

### Restart strategy

| Restart `r` | Mode |
|---|---|
| 0–1 | Strict-improvement hill climbing (safety net) |
| 2–3 | SA with `T0 = 3%` of starting Q (cold) |
| 4–7 | SA with `T0 = 5%` (medium) |
| 8–9 | SA with `T0 = 8%` (hot) |

Cold SA wins on tight axis-aligned cases; hot SA helps angled / forced-angle cases escape larger local minima. Two pure hill restarts guarantee we never regress from a deterministic baseline.

### Operators

The hill loop picks one operator per iteration via an adaptive distribution that warms up uniformly and then biases toward operators with a high recent improvement rate.

| Operator | What it does |
|---|---|
| `add_bay` | Greedy single placement at a candidate point |
| `replace_bay` | Remove one bay, add a different one in its place |
| `fill_aggressive` | Add up to 5 bays in one step |
| `remove_k_and_refill` | Remove k bays, add k+3 (escape local optima) |
| `shared_gap_refill` | Remove k bays, refill with gap-sharing pairs |
| `upgrade_bay` | Swap a bay's type/angle in-place |
| `rotate_compact_and_add` | Rotate to compact, then try a new add |
| `add_45_degree_bay` | Insert a bay at 45° (angled cases) |
| `position_perturb_and_add` | Small position delta + follow-up add |
| `split_bay` | Split one bay into two smaller ones |

### Solution accumulator (`struct QSums`)

Maintains `Σprice`, `Σloads`, `Σarea` incrementally so that:

```
quality_with_added(cur, p, l, a, wh_area)   // O(1)
quality_from_sums(sums, wh_area, empty)     // O(1)
```

This makes the inner loop hot path collision-bound rather than score-bound.

### Tuning constants (top of `solver.cpp`)

```cpp
const int    ITERATIONS            = 450;     // hill/SA loop cap
const int    INITIAL_ADDS          = 80;      // initial-solution placement budget
const int    RESTARTS              = 10;
const int    ANGLE_SAMPLE          = 14;      // 360° angle search resolution
const double CASE_BUDGET_SECONDS   = 25.0;    // wall-time deadline per case
```

Each hill loop stops on `Clock::now() < deadline && iter < ITERATIONS`. The judge limit is 30s; budget is 25s with margin. Bump `ITERATIONS` to a large value (e.g. 100 000) if you want pure deadline-bound behaviour.

## Geometry invariants

- **`rect_inside_polygon`** uses vertical ray-casting at the mid-points of sub-intervals — do *not* simplify to a naive AABB-vs-bbox check; concave warehouses break it.
- **`min_ceiling_between(x1, x2, ceiling)`** is a linear scan; the ceiling is a step function defined by `(x_start, height)` rows, with the last row extending to infinity.
- Two rectangles overlap iff `not (ax+aw≤bx or bx+bw≤ax or ay+ad≤by or by+bd≤ay)`.

## Test cases

29 bundled cases in this directory:

- `Case0`–`Case3`, `Case40`, `CaseWeird`, `CaseForcedAngle` — the original challenge cases.
- `CaseAngled{A..D}`, `CaseDiagArm{A..D}` — angled-warehouse stress tests.
- `EdgeCase01..04`, `HardCase01..10` — edge cases (tiny warehouse, impossible heights, mazes, narrow corridors, ceiling traps, etc.).

Each is a self-contained directory with the four input CSVs.

## Visualisers

```bash
python3 visualize.py path/to/case_dir       # 2D layout
python3 visualize_3d.py path/to/case_dir    # 3D with stepped ceiling
```

Requires `pandas` and `matplotlib`. The frontend's Canvas component renders the same data interactively.

## Best known Q scores (this solver)

| Case | Q |
|---|---|
| Case0 | 1125 (deadline-bound, 25s) |

Lower is better. Other cases TBD.
