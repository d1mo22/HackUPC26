# Frontend Plan — Warehouse Optimizer UI

**Stack**: React + TypeScript + Vite + Tailwind CSS + Canvas 2D + Node/Express  
**Team**: David (canvas / rendering) · Ferran (data / server / panels)  
**Repo layout**: `frontend/src/` for React, `frontend/server/` for Express bridge

---

## Already done ✅

| File | What it does |
|------|-------------|
| `src/types.ts` | All shared types: `BayType`, `PlacedBay`, `WarehouseCase`, `Solution`, etc. |
| `src/lib/csvParser.ts` | PapaParse wrappers for all 5 CSVs |
| `src/lib/geometry.ts` | `bayDimensions`, `minCeilingBetween`, `polygonBounds` |
| `src/lib/scoring.ts` | `computeMetrics` → Q, coverage %, price, loads |
| `src/hooks/useCanvas.ts` | Zoom (wheel) + pan (drag) transform state |
| Tailwind + Vite | Configured and working |

---

## Design tokens (reference for both)

```
Background:   #020617    Card/panel:  #0E1223
Border:       #334155    Muted text:  #94A3B8
Foreground:   #F8FAFC    Accent:      #22C55E
Destructive:  #EF4444
```

Fonts: **Inter** (UI) · **JetBrains Mono** (numbers/metrics)

Bay colors (one per type id, cycling):
`#3B82F6 #F59E0B #EC4899 #8B5CF6 #06B6D4 #F97316 #84CC16 #14B8A6`

---

## Tasks

---

### T1 — Design tokens + fonts
**Owner**: David  
**Depends on**: nothing  
**Files**: `src/index.css`, `index.html`

Add Inter + JetBrains Mono from Google Fonts in `index.html`. Define CSS variables for the color tokens and font families in `index.css` (`:root` block). These variables will be used by every component.

**Done when**: `var(--color-accent)` resolves to `#22C55E` in the browser, both fonts load.

---

### T2 — App shell layout
**Owner**: David  
**Depends on**: T1  
**Files**: `src/App.tsx`

3-column layout that fills the viewport:
- **Left panel** (260px fixed): file loader + run button
- **Center**: canvas takes all remaining space (`flex-1`)
- **Right panel** (280px fixed): metrics + legend

State that lives in `App.tsx` (pass down as props):
```ts
warehouseCase: WarehouseCase | null
solution: Solution | null
isRunning: boolean
```

**Done when**: three columns are visible at full height with the correct background colors, empty but structurally correct.

---

### T3 — Topbar
**Owner**: Ferran  
**Depends on**: T1  
**Files**: `src/components/Topbar.tsx`

Single horizontal bar at the top (height ~48px). Contains:
- App title: `"Warehouse Optimizer"` in JetBrains Mono
- Status badge (right side): `"No data"` / `"Ready"` / `"Running…"` — color changes with state (muted / accent / yellow)

Props: `status: 'idle' | 'ready' | 'running' | 'error'`

**Done when**: bar renders with correct colors and badge changes color based on prop.

---

### T4 — FileLoader component
**Owner**: Ferran  
**Depends on**: T1, `src/lib/csvParser.ts`  
**Files**: `src/components/FileLoader.tsx`

4 drag-and-drop slots, one per required CSV:
- `warehouse.csv`
- `obstacles.csv`
- `ceiling.csv`
- `types_of_bays.csv`

Each slot shows: filename label + a drop zone. When a file is dropped or selected:
1. Parse it with the matching function from `csvParser.ts`
2. Show a green checkmark + filename on success
3. Show a red error message on parse failure

When all 4 are loaded, call `onCaseLoaded(warehouseCase: WarehouseCase)` prop.

Also: a separate smaller slot for loading an existing `solution.csv` (optional, for previewing without running solver). Calls `onSolutionLoaded(solution: Solution)`.

**Done when**: drop all 4 CSVs from `solver/Case0/`, component calls `onCaseLoaded` with correct data (verify in console).

---

### T5 — Express server scaffold
**Owner**: Ferran  
**Depends on**: nothing (pure Node)  
**Files**: `server/index.ts`, `server/tsconfig.json`, `package.json` (add `server` scripts)

Small Express server at port `3001`:

```
POST /solve
  body: multipart/form-data with fields:
    warehouse, obstacles, ceiling, types  (File each)
  response: text/csv  (solution.csv contents)
```

Flow:
1. Receive 4 files via `multer` into a temp directory
2. Spawn `python3 ../../solver/solver.py` — **wait**, it currently runs all cases. We need to adapt this. For now, copy the 4 files as `Case0/` structure and run the solver, then read back `Case0/solution.csv`.
3. Stream the CSV text back as the response.
4. Clean up temp files.

Add to `package.json`:
```json
"server": "ts-node server/index.ts",
"dev:server": "nodemon --watch server server/index.ts"
```

Also add a Vite proxy in `vite.config.ts` so `fetch('/solve', ...)` in the browser forwards to `localhost:3001`.

**Done when**: `curl -X POST http://localhost:3001/solve -F warehouse=@Case0/warehouse.csv ...` returns a valid CSV.

---

### T6 — useSolver hook
**Owner**: Ferran  
**Depends on**: T5  
**Files**: `src/hooks/useSolver.ts`

```ts
const { run, isRunning, error } = useSolver()
run(warehouseCase: WarehouseCase): Promise<Solution>
```

Internally:
1. Serialize the `WarehouseCase` back to CSV strings (use the raw `File` objects stored in state, not re-serialize)
2. `POST /solve` with FormData
3. Parse the response CSV with `parseSolution`
4. Return the `Solution`

**Note**: store the raw `File` objects in `App.tsx` state alongside the parsed `WarehouseCase` so this hook can send them directly.

**Done when**: clicking "Run Solver" in the UI sends the request and returns a parsed `Solution`.

---

### T7 — Canvas: warehouse + obstacles
**Owner**: David  
**Depends on**: T2, `src/hooks/useCanvas.ts`, `src/lib/geometry.ts`  
**Files**: `src/components/Canvas.tsx`

Render the warehouse on a `<canvas>` element that fills its container. Use `useCanvas` for zoom/pan.

Transform logic:
- Warehouse coords are in mm, origin bottom-left
- Canvas origin is top-left → **invert Y**: `canvasY = (maxY - worldY) * scale + offsetY`
- Fit-to-screen on first load: compute a scale that fits the polygon bounding box with 40px padding

Draw order:
1. Fill canvas with `#020617`
2. Draw warehouse polygon — stroke `#475569` 2px, fill `#0E1223`
3. Draw each obstacle — fill `#1E293B`, stroke `#334155` 1px, hatching pattern (diagonal lines every 6px, color `#334155`)

Props:
```ts
warehouseCase: WarehouseCase | null
solution: Solution | null
```

Use `requestAnimationFrame` for the render loop (re-render when transform changes).

**Done when**: Case0 warehouse polygon and 3 obstacles render correctly, zoom and pan work.

---

### T8 — Canvas: bay rendering
**Owner**: David  
**Depends on**: T7  
**Files**: `src/components/Canvas.tsx`

Extend the canvas render to draw placed bays on top of the warehouse.

Bay colors: cycle through the palette by `bayType.id % 8`. Use `rgba(color, 0.75)` fill, full color stroke 1.5px.

Per bay:
1. Compute effective `w, d` from `bayDimensions(placement, type)`
2. Draw filled + stroked rectangle
3. Draw the bay type `id` centered in the rectangle (JetBrains Mono, white, sized to fit)
4. Draw a small triangle in the top-right corner to indicate rotation (pointing right = rot 0, pointing up = rot 1)

**Done when**: Case0 solution renders all 12 bays with correct positions, sizes, rotations, and colors.

---

### T9 — Canvas: ceiling overlay
**Owner**: David  
**Depends on**: T7  
**Files**: `src/components/Canvas.tsx`

Add an optional ceiling overlay toggle (boolean prop `showCeiling`).

When enabled, for each ceiling segment `[x, nextX)` with height `h`:
- Draw a semi-transparent rect over that x-band from top of the warehouse
- Opacity proportional to `1 - h/maxH` (darker = lower ceiling)
- Color: `rgba(59, 130, 246, 0.15)` (blue tint)

**Done when**: ceiling overlay visually shows the step function on the canvas, clearly distinguishable.

---

### T10 — Canvas: hit-testing + tooltip
**Owner**: David  
**Depends on**: T8  
**Files**: `src/components/Canvas.tsx`, `src/components/BayTooltip.tsx`

On `mousemove` over the canvas:
1. Convert mouse coords to world coords (inverse transform)
2. Check which bay's bounding box contains the point (iterate in reverse draw order)
3. If found, set `hoveredBay` state

`BayTooltip.tsx` — absolutely positioned `div` near the mouse showing:
- Bay type id
- Dimensions: `{w} × {d} mm`
- Height: `{h} mm`
- Price: `{price}`
- Loads: `{loads}`

**Done when**: hovering over any bay shows the tooltip with correct data; moving off hides it.

---

### T11 — MetricsPanel component
**Owner**: Ferran  
**Depends on**: T2, `src/lib/scoring.ts`  
**Files**: `src/components/MetricsPanel.tsx`

Receives `solution: Solution | null` and `warehouseCase: WarehouseCase | null`.  
Calls `computeMetrics` and renders:

```
Q SCORE
4.82                ← large, JetBrains Mono, accent green

Coverage    73.4%
Total price 48,200
Total loads 312
Bay count   12
```

When `solution` is null, show placeholder dashes.  
Numbers animate from 0 on mount/change (simple CSS transition or a small counter hook).

**Done when**: loading Case0 solution shows correct Q, coverage, price, loads, count.

---

### T12 — BayLegend component
**Owner**: Ferran  
**Depends on**: T2  
**Files**: `src/components/BayLegend.tsx`

Renders below MetricsPanel. One row per bay type that appears in the current solution:

```
● Type 0   800×1200   4 loads   2000€
● Type 1  1600×1200   8 loads   2500€
```

Colored dot (`●`) uses the same bay palette as T8.  
If no solution loaded, renders nothing.

**Done when**: all bay types from Case0 solution appear with correct data and matching colors.

---

### T13 — Run Solver button + controls
**Owner**: Ferran  
**Depends on**: T6, T4  
**Files**: `src/components/Controls.tsx`

Full-width button at the bottom of the left panel:
- Disabled + grey when `warehouseCase` is null
- Green + "Run Solver" when ready
- Spinner + "Running…" while `isRunning`
- Red + "Error" briefly on failure

Also include:
- Toggle switch: "Show ceiling overlay" (controls `showCeiling` prop passed to Canvas)
- Toggle switch: "Show bay labels"

Props: `onRun`, `isRunning`, `isReady`, `showCeiling`, `onToggleCeiling`, `showLabels`, `onToggleLabels`

**Done when**: button triggers the solver, disables during run, re-enables on completion.

---

### T14 — Wire everything in App.tsx
**Owner**: David + Ferran (together)  
**Depends on**: T3, T4, T5, T6, T11, T12, T13, T7, T8  
**Files**: `src/App.tsx`

Connect all state and props:
- `FileLoader` → sets `warehouseCase` + raw files in state
- `Controls` run button → calls `useSolver().run()` → sets `solution` in state
- `Canvas` receives `warehouseCase`, `solution`, `showCeiling`, `showLabels`
- `MetricsPanel` + `BayLegend` receive `solution` + `warehouseCase`
- `Topbar` receives derived `status`

**Done when**: full end-to-end flow works: load CSVs → run solver → see bays on canvas + metrics panel update.

---

## Task dependency graph

```
T1 ──► T2 ──► T7 ──► T8 ──► T10
              T7 ──► T9
T1 ──► T3
T1 ──► T4 ──────────────────────► T14
T5 ──► T6 ──────────────────────► T14
T1 ──► T11 ─────────────────────► T14
T1 ──► T12 ─────────────────────► T14
T6 ──► T13 ─────────────────────► T14
```

## Recommended order

| Phase | David | Ferran |
|-------|-------|--------|
| 1 | T1 → T2 | T3 → T4 |
| 2 | T7 (warehouse render) | T5 (server) → T6 (hook) |
| 3 | T8 (bay render) | T11 (metrics) → T12 (legend) |
| 4 | T9 (ceiling) + T10 (tooltip) | T13 (controls) |
| 5 | **T14 — wire together (both)** | |

---

## Contracts between David and Ferran

These are the interfaces that must match — agree before T14.

**`App.tsx` state shape**
```ts
warehouseCase: WarehouseCase | null
rawFiles: { warehouse: File; obstacles: File; ceiling: File; types: File } | null
solution: Solution | null
isRunning: boolean
showCeiling: boolean
showLabels: boolean
```

**Canvas props**
```ts
warehouseCase: WarehouseCase | null
solution: Solution | null
showCeiling: boolean
showLabels: boolean
```

**`/solve` endpoint**
- Method: `POST`
- Body: `multipart/form-data` fields `warehouse`, `obstacles`, `ceiling`, `types`
- Response: `text/csv` (raw solution CSV with header `Id,X,Y,Rotation`)
- Error: HTTP 500 with plain text error message
