# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

HackUPC 2026 — Mecalux Warehouse Optimizer. Given a warehouse polygon, obstacles, a stepped ceiling, and bay type catalog, produce a 2D bay placement that minimises the metric:

```
Q = (Σ price/loads)^(2 - area_bays/area_warehouse)
```

## Repo layout

```
solver/          # Python solver (already working)
  solver.py      # main solver — hill climbing over add/move/rotate/compact/reinsert ops
  visualize.py   # 2D matplotlib visualisation
  visualize_3d.py
  Case0–Case3/   # input CSVs + solution.csv per case
frontend/        # React+Vite UI (in development — owner: David)
plan.md          # full architecture doc — read this for design decisions
```

## Solver CLI

The solver is pure Python with no CLI flags; it iterates over `Case0`–`Case3` directories relative to the working directory:

```bash
cd solver && python solver.py
```

Output is written to `Case{N}/solution.csv` (columns: `Id,X,Y,Rotation`).

## CSV formats

All coordinates are in **millimetres**, origin bottom-left.

| File | Columns |
|------|---------|
| `warehouse.csv` | `x,y` — polygon vertices in order |
| `obstacles.csv` | `x,y,w,d` — axis-aligned rectangles |
| `ceiling.csv` | `x,h` — stepped ceiling; segment `[x, next_x)` has height `h` |
| `types_of_bays.csv` | `id,w,d,h,gap,loads,price` |
| `solution.csv` | `Id,X,Y,Rotation` — `Rotation` is `0` or `1` (not degrees) |

`Rotation=0` → w×d as-is. `Rotation=1` → w and d are swapped (90°).  
`gap` is a clearance zone added **after** the bay: rot=0 adds `(x, y+d, w, gap)`; rot=1 adds `(x+w, y, gap, d)`.

## Frontend stack

React + TypeScript + Vite + Tailwind CSS + Canvas 2D API (native). PapaParse for CSV parsing. Small Node/Express bridge to spawn `solver.py` and return `solution.csv`.

**Important:** Tailwind utility classes for layout/spacing/color are unreliable in this setup — use inline `style={{}}` props for everything. Tailwind structural classes (`flex`, `flex-1`, `overflow-hidden`) still work.

See `frontend/FRONTEND_PLAN.md` for the full task breakdown.

## Frontend status (as of 2026-04-25)

### Done ✅

| Task | Component/File | Notes |
|------|---------------|-------|
| T1 | `src/index.css`, `index.html` | CSS variables, Google Fonts |
| T2 | `src/App.tsx` | 3-column shell, StatusBadge |
| T3 | `src/App.tsx` | Topbar inline in App |
| T4 | `src/components/FileLoader.tsx` | 2×2 grid + solution slot, validation |
| T5 | `server/index.ts`, `server/run_single.py` | Express POST /solve |
| T6 | Wired in `App.tsx` `handleRun()` | fetch /solve, parse response |
| T11 | `src/components/MetricsPanel.tsx` | Q score, coverage, price, loads, animated |
| T12 | `src/components/BayLegend.tsx` | Per-type color swatch + stats |
| T13 | `src/components/Controls.tsx` | Toggles, Run Solver, Export CSV |
| Intelligence Panel | `src/components/RightPanel.tsx` | 3 tabs: Metrics · Breakdown · History |
| Breakdown tab | `src/components/BreakdownTab.tsx` | price/loads bar chart, tip card |
| History tab | `src/components/HistoryTab.tsx` | Past runs, restore on canvas |
| Export CSV | `src/components/Controls.tsx` | Client-side blob download |
| CSV validation | `src/lib/csvParser.ts` | Header skip, column names in errors |
| Types | `src/types.ts` | Includes `RunRecord` |

### Remaining ⏳

| Task | Owner | Blocked on |
|------|-------|-----------|
| T7 — Canvas: warehouse + obstacles | Ferran | — |
| T8 — Canvas: bay rendering | Ferran | T7 |
| T9 — Canvas: ceiling overlay | Ferran | T7 |
| T10 — Canvas: hit-testing + tooltip | Ferran | T8 |
| T14 — Wire canvas into App | Both | T7–T10 |
| Split-canvas comparison view | David | Ferran's canvas prop-driven |

### To start the app

```bash
# Terminal 1 — frontend dev server
cd frontend && npm run dev

# Terminal 2 — solver bridge (needed for Run Solver button)
cd frontend && npm run dev:server
```

## Key geometry invariants

- `rect_inside_polygon` uses vertical ray-casting at mid-points of sub-intervals — do not replace with a naive AABB check.
- `min_ceiling_between(x1, x2, ceiling)` is a linear scan; the ceiling is a step function defined by `(x_start, height)` pairs with the last segment extending to infinity.
- Two rectangles overlap iff `not (ax+aw≤bx or bx+bw≤ax or ay+ad≤by or by+bd≤ay)`.
