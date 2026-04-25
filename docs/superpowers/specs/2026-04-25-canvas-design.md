# Canvas Component Design Spec

## Goal

Build a 2D Canvas component (T7–T10 + T14) that renders the warehouse polygon, obstacles, bay placements, and ceiling overlay. Supports zoom/pan, a 2D↔3D toggle, a fit-to-screen button, and a full hover tooltip.

---

## Architecture

A single `Canvas.tsx` component owns all rendering. It receives warehouse data and solution as props and calls the existing `useCanvas.ts` hook for zoom/pan transform state. All drawing uses the native Canvas 2D API — no third-party rendering library.

Two render modes share the same data pipeline:
- **2D mode**: top-down orthographic view, Y-axis inverted (warehouse bottom-left → canvas top-left)
- **3D mode**: isometric projection, bays as solid 5-face boxes, ceiling as floating dashed planes

Mode state (`'2d' | '3d'`) lives inside `Canvas.tsx`. Everything else (solution, warehouseCase) comes from props passed down from `App.tsx`.

The canvas renders on every prop change and on every transform change from `useCanvas`. `requestAnimationFrame` is used for smooth zoom/pan redraws.

---

## HiDPI Resolution

```ts
const dpr = window.devicePixelRatio || 1
canvas.width  = logicalWidth  * dpr
canvas.height = logicalHeight * dpr
ctx.scale(dpr, dpr)
// All drawing coordinates remain in logical pixels
```

The CSS `width`/`height` stays at logical size. This must be applied once on mount and re-applied if the canvas element is resized.

---

## Bay Rendering (2D mode)

Each `PlacedBay` is drawn as a filled rectangle using `getBayColor(id)` from `BayLegend.tsx`.

**Dimensions**: `bayDimensions(bay, type)` from `geometry.ts` — returns `{ w, d }` already accounting for rotation.

**Label**: type ID centered on the bay. Font: `bold 9px JetBrains Mono`. Only drawn if the bay is large enough to fit text (min 12px in both dimensions after transform).

**Gap zone**: a dashed semi-transparent rectangle placed adjacent to the bay in the gap direction:
- `rotation === 0`: gap rect at `(x, y + d, w, gap)` — below bay in warehouse coords = above bay on canvas (Y-flipped)
- `rotation === 1`: gap rect at `(x + w, y, gap, d)` — to the right
- Arbitrary angles: gap rect drawn using `ctx.save() → ctx.translate(cx, cy) → ctx.rotate(angle) → draw → ctx.restore()`
- Fill: `bayColor` at 12% opacity. Stroke: `bayColor` at 55% opacity, `[3, 2]` dash.
- Toggled by a **Show gaps** checkbox in `Controls.tsx` (prop `showGaps: boolean` passed to `Canvas`).

**Hover highlight**: hovered bay gets a 1.5px white border. Hit-testing via reverse iteration of the placement array, checking AABB in world space, then converting to canvas coords.

---

## Bay Rendering (3D / Isometric mode)

Isometric projection:
```ts
const C30 = Math.cos(Math.PI / 6)
const S30 = Math.sin(Math.PI / 6)

function iso(wx: number, wy: number, wz: number, origin: Point, scale: number) {
  return {
    x: origin.x + (wx - wy) * scale * C30,
    y: origin.y + (wx + wy) * scale * S30 - wz * scale,
  }
}
```

Each bay is a 5-face closed box. Draw order per box (back-to-front to avoid z-fighting):

| Face | World position | Shade factor |
|------|---------------|-------------|
| Back | `y = by + bd` | 0.28 |
| Left | `x = bx` | 0.38 |
| Right | `x = bx + bw` | 0.52 |
| Front | `y = by` | 0.65 |
| Top | `z = bh` | 1.00 |

Shade helper: `rgba(r*f, g*f, b*f, 1)` where `[r,g,b]` = parsed hex of `getBayColor(id)`.

Each face gets a `rgba(0,0,0,0.5)` edge stroke at 0.5px. Top face gets a `rgba(255,255,255,0.2)` stroke. A `rgba(255,255,255,0.4)` highlight line is drawn on the front-top edge.

Bay boxes are sorted by `(x + y)` ascending before drawing (painter's algorithm).

Type ID label drawn at the top-face center, `bold 8px monospace`, `rgba(255,255,255,0.92)`.

**Gap zone in 3D**: not shown (ceiling planes already communicate the constraint; gap zones would add visual noise in isometric).

---

## Ceiling Overlay

### 2D mode (toggled by **Show ceiling** in Controls)

For each `CeilingSegment { x, h }`:
1. **Tint band**: vertical strip from `x` to next segment's `x`, full canvas height. Fill: `rgba(0,0,0, (1 - h/maxH) * 0.38)`. Darker = lower ceiling.
2. **Step line**: a polyline connecting `(seg.x, h_scaled)` steps, drawn in `#ef4444` at 1.5px. Area above the line filled `rgba(239,68,68,0.07)`.
3. **Height label**: `h` value in mm at the top of each band, `8px monospace`, colored per segment (`#22c55e` for tallest, `#ef4444` for shortest, `#eab308` for mid).

### 3D mode (always shown, no toggle needed)

Ceiling segments drawn as floating dashed horizontal planes after all bays:
- Fill: low-opacity color per segment height (green → yellow → red)
- Stroke: 1px dashed `[4, 3]`, same color
- Height label near front edge of each plane

---

## Canvas Overlay Controls (top-right corner)

Two controls rendered as absolutely-positioned HTML over the canvas (not drawn on canvas):

```
[ 2D | 3D ]   [ ⊡ Fit ]
```

- **2D / 3D toggle**: `background: #0f172a`, `border: 1px solid #334155`, active button gets `background: #1e293b`, `color: var(--color-fg)`. State: `viewMode` in `Canvas.tsx`.
- **Fit button**: resets transform to fit the warehouse bounding box inside the canvas with 32px padding. Calls `setTransform({ scale, offsetX, offsetY })` from `useCanvas`.

Fit-to-screen calculation (transform convention: `screen_x = offsetX + wx * scale`, `screen_y = offsetY - wy * scale`):
```ts
const padding = 32
const bounds  = polygonBounds(polygon)  // from geometry.ts
const scaleX  = (canvasW - 2 * padding) / (bounds.maxX - bounds.minX)
const scaleY  = (canvasH - 2 * padding) / (bounds.maxY - bounds.minY)
const scale   = Math.min(scaleX, scaleY)
// offsetX: left edge of warehouse lands at padding px from left
const offsetX = padding - bounds.minX * scale
// offsetY: top edge of warehouse (maxY in world) lands at padding px from top
const offsetY = padding + bounds.maxY * scale
```

---

## Tooltip (2D mode only)

Shown on hover over a placed bay. Rendered as an absolutely-positioned `<div>` over the canvas (not drawn on canvas — avoids redraw on mouse move).

Position: follows mouse, offset `(12, 12)`, clamps to canvas edges so it never overflows.

Content:
```
Type {id}           ← colored dot + bold label
SIZE    {w} × {d} mm
HEIGHT  {h} mm
GAP     {gap} mm
LOADS   {loads}
PRICE   {price}
Q RATIO {(price/loads).toFixed(2)}  [★ best | ↓ worst]
```

The Q ratio badge uses `var(--color-accent)` for best type, `var(--color-destructive)` for worst — same logic as `BreakdownTab.tsx`.

Style: `background: var(--color-card)`, `border: 1px solid var(--color-border)`, `border-radius: 6px`, `padding: 8px 10px`, `font-family: var(--font-mono)`, `font-size: 11px`.

Tooltip is hidden when: no bay is hovered, or solution is null, or view is in 3D mode.

---

## Props Interface

```ts
interface CanvasProps {
  polygon:     Point[]          | null
  obstacles:   Obstacle[]       | null
  ceiling:     CeilingSegment[] | null
  bayTypes:    BayType[]        | null
  placements:  PlacedBay[]      | null
  showGaps:    boolean
  showCeiling: boolean
}
```

`App.tsx` passes these from `warehouseCase` and `solution`. `showGaps` and `showCeiling` come from toggle state in `Controls.tsx`.

---

## Files

| Action | Path |
|--------|------|
| Create | `frontend/src/components/Canvas.tsx` |
| Modify | `frontend/src/components/Controls.tsx` — add `showGaps`, `showCeiling` toggles and their callbacks |
| Modify | `frontend/src/App.tsx` — wire `showGaps`/`showCeiling` state, pass to `Canvas` |
| Read   | `frontend/src/hooks/useCanvas.ts` — zoom/pan transform, `setTransform` |
| Read   | `frontend/src/lib/geometry.ts` — `bayDimensions`, `polygonBounds` |
| Read   | `frontend/src/components/BayLegend.tsx` — `getBayColor` |
| Read   | `frontend/src/types.ts` — `Point`, `Obstacle`, `CeilingSegment`, `BayType`, `PlacedBay` |
