# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

HackUPC 2026 — Mecalux Warehouse Optimizer. Given a warehouse polygon, obstacles, a stepped ceiling, and bay type catalog, produce a 2D bay placement that maximises the metric:

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

See `plan.md` §6 for the full component breakdown and integration approach.

## Key geometry invariants

- `rect_inside_polygon` uses vertical ray-casting at mid-points of sub-intervals — do not replace with a naive AABB check.
- `min_ceiling_between(x1, x2, ceiling)` is a linear scan; the ceiling is a step function defined by `(x_start, height)` pairs with the last segment extending to infinity.
- Two rectangles overlap iff `not (ax+aw≤bx or bx+bw≤ax or ay+ad≤by or by+bd≤ay)`.
