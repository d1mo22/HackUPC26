import { useRef, useState, useEffect, useCallback } from 'react'
import { useCanvas, type Transform } from '../hooks/useCanvas'
import { getBayColor } from './BayLegend'
import { bayDimensions, polygonBounds } from '../lib/geometry'
import type { Point, Obstacle, CeilingSegment, BayType, PlacedBay } from '../types'
import Canvas3D from './Canvas3D'

export interface CanvasProps {
  polygon: Point[] | null
  obstacles: Obstacle[] | null
  ceiling: CeilingSegment[] | null
  bayTypes: BayType[] | null
  placements: PlacedBay[] | null
  showGaps: boolean
  showCeiling: boolean
  showLabels: boolean
  selectedTypeIds: Set<number>
  onViewModeChange?: (mode: '2d' | '3d') => void
  viewModeOverride?: '2d' | '3d'
  onFitRef?: (fn: () => void) => void
  onCanvasRef?: (canvas: HTMLCanvasElement | null) => void
}

// ─── Coordinate helpers ────────────────────────────────────────────────────

function w2s(wx: number, wy: number, t: Transform) {
  return { x: t.offsetX + wx * t.scale, y: t.offsetY - wy * t.scale }
}


// ─── 2D drawing ────────────────────────────────────────────────────────────

function draw2D(
  ctx: CanvasRenderingContext2D, _W: number, H: number, t: Transform,
  polygon: Point[], obstacles: Obstacle[], ceiling: CeilingSegment[],
  bayTypes: BayType[], placements: PlacedBay[],
  showGaps: boolean, showCeiling: boolean, showLabels: boolean,
  hoveredIdx: number | null,
  theme: { canvasBg: string; canvasFloor: string; canvasGrid: string },
) {
  const typeMap = new Map(bayTypes.map(bt => [bt.id, bt]))
  const bounds = polygonBounds(polygon)

  // Warehouse polygon
  ctx.beginPath()
  for (let i = 0; i < polygon.length; i++) {
    const p = w2s(polygon[i].x, polygon[i].y, t)
    if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y)
  }
  ctx.closePath()
  ctx.fillStyle = theme.canvasFloor
  ctx.fill()
  ctx.strokeStyle = theme.canvasGrid
  ctx.lineWidth = 1.5
  ctx.stroke()

  // Ceiling overlay — column tint clipped to warehouse polygon (darker = lower ceiling)
  if (showCeiling && ceiling.length > 0) {
    const sorted = [...ceiling].sort((a, b) => a.x - b.x)
    const maxH = Math.max(...sorted.map(s => s.h))

    ctx.save()
    // Clip to warehouse polygon so tint doesn't bleed outside
    ctx.beginPath()
    for (let i = 0; i < polygon.length; i++) {
      const p = w2s(polygon[i].x, polygon[i].y, t)
      if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y)
    }
    ctx.closePath()
    ctx.clip()

    for (let i = 0; i < sorted.length; i++) {
      const seg = sorted[i]
      const nextX = i + 1 < sorted.length ? sorted[i + 1].x : bounds.maxX
      const alpha = (1 - seg.h / maxH) * 0.38
      ctx.fillStyle = `rgba(0,0,0,${alpha.toFixed(3)})`
      const topY = t.offsetY - bounds.maxY * t.scale
      ctx.fillRect(t.offsetX + seg.x * t.scale, topY, (nextX - seg.x) * t.scale, H - topY)
    }

    ctx.restore()
  }
      ctx.textBaseline = 'top'

  // Obstacles
  for (const obs of obstacles) {
    const sx = t.offsetX + obs.x * t.scale
    const sy = t.offsetY - (obs.y + obs.d) * t.scale
    const sw = obs.w * t.scale
    const sh = obs.d * t.scale
    ctx.fillStyle = 'rgba(239,68,68,0.25)'
    ctx.fillRect(sx, sy, sw, sh)
    ctx.strokeStyle = '#ef4444'
    ctx.lineWidth = 1
    ctx.strokeRect(sx, sy, sw, sh)
    if (sw > 24 && sh > 12) {
      ctx.fillStyle = '#ef4444'
      ctx.font = 'bold 9px monospace'
      ctx.textAlign = 'center'
      ctx.textBaseline = 'middle'
      ctx.fillText('OBS', sx + sw / 2, sy + sh / 2)
    }
  }

  // Bays
  // Pass 1: fill + gap zones + labels
  for (let i = 0; i < placements.length; i++) {
    const p = placements[i]
    const type = typeMap.get(p.id)
    if (!type) continue
    const dims = bayDimensions(p, type)
    const color = getBayColor(p.id)

    const sx = t.offsetX + p.x * t.scale
    const sy = t.offsetY - (p.y + dims.d) * t.scale
    const sw = dims.w * t.scale
    const sh = dims.d * t.scale

    ctx.fillStyle = color + 'cc'
    ctx.fillRect(sx, sy, sw, sh)
    ctx.strokeStyle = 'rgba(0,0,0,0.3)'
    ctx.lineWidth = 0.7
    ctx.strokeRect(sx, sy, sw, sh)

    if (showGaps && type.gap > 0) {
      const gsize = type.gap * t.scale
      ctx.fillStyle = color + '1f'
      ctx.strokeStyle = color + '8c'
      ctx.lineWidth = 0.8
      ctx.setLineDash([3, 2])
      if (p.rotation === 0) {
        // gap above (+Y)
        const gy = t.offsetY - (p.y + dims.d + type.gap) * t.scale
        ctx.fillRect(sx, gy, sw, gsize)
        ctx.strokeRect(sx, gy, sw, gsize)
      } else if (p.rotation === 90) {
        // gap to the left (-X)
        const gx = t.offsetX + (p.x - type.gap) * t.scale
        ctx.fillRect(gx, sy, gsize, sh)
        ctx.strokeRect(gx, sy, gsize, sh)
      } else if (p.rotation === 180) {
        // gap below (-Y)
        const gy = t.offsetY - p.y * t.scale
        ctx.fillRect(sx, gy, sw, gsize)
        ctx.strokeRect(sx, gy, sw, gsize)
      } else {
        // rotation === 270: gap to the right (+X)
        const gx = t.offsetX + (p.x + dims.w) * t.scale
        ctx.fillRect(gx, sy, gsize, sh)
        ctx.strokeRect(gx, sy, gsize, sh)
      }
      ctx.setLineDash([])
    }

    if (showLabels && sw >= 12 && sh >= 12) {
      ctx.fillStyle = 'rgba(255,255,255,0.9)'
      ctx.font = 'bold 9px monospace'
      ctx.textAlign = 'center'
      ctx.textBaseline = 'middle'
      ctx.fillText(String(p.id), sx + sw / 2, sy + sh / 2)
    }
  }

  // Pass 2: hover highlight drawn on top of all bays so neighbours can't cover it
  if (hoveredIdx !== null && hoveredIdx < placements.length) {
    const p = placements[hoveredIdx]
    const type = typeMap.get(p.id)
    if (type) {
      const dims = bayDimensions(p, type)
      const sx = t.offsetX + p.x * t.scale
      const sy = t.offsetY - (p.y + dims.d) * t.scale
      const sw = dims.w * t.scale
      const sh = dims.d * t.scale
      ctx.strokeStyle = 'rgba(255,255,255,0.9)'
      ctx.lineWidth = 1.5
      ctx.strokeRect(sx - 0.5, sy - 0.5, sw + 1, sh + 1)
    }
  }
}


// ─── Component ─────────────────────────────────────────────────────────────

export default function Canvas({
  polygon, obstacles, ceiling, bayTypes, placements,
  showGaps, showCeiling, showLabels, selectedTypeIds,
  onViewModeChange, viewModeOverride, onFitRef, onCanvasRef,
}: CanvasProps) {
  const visiblePlacements = placements && selectedTypeIds.size > 0
    ? placements.filter(p => selectedTypeIds.has(p.id))
    : placements
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [viewMode, setViewMode] = useState<'2d' | '3d'>('2d')
  const [hoveredIdx, setHoveredIdx] = useState<number | null>(null)
  const [mousePos, setMousePos] = useState<{ x: number; y: number } | null>(null)
  const [canvasSize, setCanvasSize] = useState({ w: 0, h: 0 })
  const autoFitRef = useRef<Point[] | null>(null)
  const viewModeRef = useRef<'2d' | '3d'>('2d')

  const { transform, setTransform, onMouseDown: on2dMouseDown, onMouseMove: onMouseMovePan, onMouseUp: on2dMouseUp } = useCanvas()

  // Keep viewModeRef in sync for native event handlers
  useEffect(() => { viewModeRef.current = viewMode }, [viewMode])

  // Sync with external override (e.g. keyboard shortcut from App)
  useEffect(() => {
    if (viewModeOverride && viewModeOverride !== viewMode) {
      setViewMode(viewModeOverride)
    }
  }, [viewModeOverride])

  // HiDPI setup + resize observer
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    onCanvasRef?.(canvas)
    function updateSize() {
      const dpr = window.devicePixelRatio || 1
      const rect = canvas!.getBoundingClientRect()
      if (rect.width === 0 || rect.height === 0) return
      canvas!.width = rect.width * dpr
      canvas!.height = rect.height * dpr
      setCanvasSize({ w: rect.width, h: rect.height })
    }
    updateSize()
    const ro = new ResizeObserver(updateSize)
    ro.observe(canvas)
    return () => ro.disconnect()
  }, [])

  // Native wheel handler — prevents browser zoom (React's onWheel is passive)
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const handler = (e: WheelEvent) => {
      e.preventDefault()
      if (viewModeRef.current !== '2d') return
      const factor = e.deltaY < 0 ? 1.1 : 0.9
      const rect = canvas.getBoundingClientRect()
      const mx = e.clientX - rect.left
      const my = e.clientY - rect.top
      setTransform(t => {
        const newS = Math.min(10, Math.max(0.05, t.scale * factor))
        return {
          scale: newS,
          offsetX: mx - (mx - t.offsetX) * (newS / t.scale),
          offsetY: my - (my - t.offsetY) * (newS / t.scale),
        }
      })
    }
    canvas.addEventListener('wheel', handler, { passive: false })
    return () => canvas.removeEventListener('wheel', handler)
  }, [setTransform])

  // Fit to screen (2D only — 3D uses AutoCamera)
  const fitToScreen = useCallback(() => {
    if (!polygon || canvasSize.w === 0 || viewMode === '3d') return
    const padding = 32
    const bounds = polygonBounds(polygon)
    const scaleX = (canvasSize.w - 2 * padding) / (bounds.maxX - bounds.minX)
    const scaleY = (canvasSize.h - 2 * padding) / (bounds.maxY - bounds.minY)
    const scale = Math.min(scaleX, scaleY)
    setTransform({
      scale,
      offsetX: padding - bounds.minX * scale,
      offsetY: padding + bounds.maxY * scale,
    })
  }, [polygon, canvasSize, viewMode, setTransform])

  // Auto-fit when polygon changes or canvas first gets a size
  useEffect(() => {
    if (polygon && canvasSize.w > 0 && polygon !== autoFitRef.current) {
      fitToScreen()
      autoFitRef.current = polygon
    }
  }, [polygon, canvasSize.w, fitToScreen])

  // Expose fitToScreen to parent via ref callback
  useEffect(() => {
    onFitRef?.(fitToScreen)
  }, [fitToScreen, onFitRef])

  // Draw (2D only — 3D is rendered by Canvas3D overlay)
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas || canvasSize.w === 0) return
    const dpr = window.devicePixelRatio || 1
    const ctx = canvas.getContext('2d')!
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    ctx.clearRect(0, 0, canvasSize.w, canvasSize.h)

    if (!polygon || viewMode === '3d') return

    // Read CSS theme variables for canvas drawing
    const style = getComputedStyle(document.documentElement)
    const canvasBg    = style.getPropertyValue('--color-canvas-bg').trim()    || '#020617'
    const canvasFloor = style.getPropertyValue('--color-canvas-floor').trim() || '#08111f'
    const canvasGrid  = style.getPropertyValue('--color-canvas-grid').trim()  || '#0d1a2e'
    const themeColors = { canvasBg, canvasFloor, canvasGrid }

    draw2D(
      ctx, canvasSize.w, canvasSize.h, transform,
      polygon, obstacles ?? [], ceiling ?? [],
      bayTypes ?? [], visiblePlacements ?? [],
      showGaps, showCeiling, showLabels, hoveredIdx, themeColors,
    )
  }, [canvasSize, transform, viewMode, polygon, obstacles, ceiling, bayTypes, visiblePlacements, showGaps, showCeiling, showLabels, hoveredIdx])

  const handleMouseDown = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    on2dMouseDown(e)
  }, [on2dMouseDown])

  const handleMouseUp = useCallback(() => {
    on2dMouseUp()
  }, [on2dMouseUp])

  const handleMouseMove = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    onMouseMovePan(e)
    if (!visiblePlacements || !bayTypes) { setHoveredIdx(null); setMousePos(null); return }
    const rect = canvasRef.current!.getBoundingClientRect()
    const mx = e.clientX - rect.left
    const my = e.clientY - rect.top
    setMousePos({ x: mx, y: my })
    const typeMap = new Map(bayTypes.map(bt => [bt.id, bt]))
    let found = -1
    for (let i = visiblePlacements.length - 1; i >= 0; i--) {
      const p = visiblePlacements[i]
      const type = typeMap.get(p.id)
      if (!type) continue
      const dims = bayDimensions(p, type)
      const sx = transform.offsetX + p.x * transform.scale
      const sy = transform.offsetY - (p.y + dims.d) * transform.scale
      const sw = dims.w * transform.scale
      const sh = dims.d * transform.scale
      if (mx >= sx && mx <= sx + sw && my >= sy && my <= sy + sh) { found = i; break }
    }
    setHoveredIdx(found >= 0 ? found : null)
    if (found < 0) setMousePos(null)
  }, [visiblePlacements, bayTypes, transform, onMouseMovePan])

  const handleMouseLeave = useCallback(() => {
    setHoveredIdx(null)
    setMousePos(null)
  }, [])

  // Tooltip content
  const tooltip = (() => {
    if (mousePos === null || hoveredIdx === null || !visiblePlacements || !bayTypes || viewMode !== '2d') return null
    const p = visiblePlacements[hoveredIdx]
    const type = bayTypes.find(t => t.id === p.id)
    if (!type) return null
    const color = getBayColor(p.id)
    const dims = bayDimensions(p, type)
    const ratio = type.price / type.loads
    const allRatios = bayTypes.map(t => t.price / t.loads)
    const best = Math.min(...allRatios)
    const worst = Math.max(...allRatios)
    const isBest = Math.abs(ratio - best) < 0.001
    const isWorst = Math.abs(ratio - worst) < 0.001
    const canvas = canvasRef.current!
    const cW = canvas.getBoundingClientRect().width
    const cH = canvas.getBoundingClientRect().height
    const ttW = 196, ttH = 160
    let tx = mousePos.x + 14
    let ty = mousePos.y + 14
    if (tx + ttW > cW) tx = mousePos.x - ttW - 6
    if (ty + ttH > cH) ty = mousePos.y - ttH - 6
    return { color, type, dims, ratio, isBest, isWorst, tx, ty }
  })()

  return (
    <div style={{ position: 'relative', width: '100%', height: '100%' }}>
      <canvas
        ref={canvasRef}
        style={{
          display: 'block', width: '100%', height: '100%',
          cursor: hoveredIdx !== null ? 'pointer' : 'grab',
          visibility: viewMode === '3d' ? 'hidden' : 'visible',
        }}
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={handleMouseLeave}
      />

      {/* r3f 3D view — mounted always so it initialises, shown/hidden by pointer-events + visibility */}
      {polygon && (
        <div style={{
          position: 'absolute', inset: 0,
          visibility: viewMode === '3d' ? 'visible' : 'hidden',
          pointerEvents: viewMode === '3d' ? 'auto' : 'none',
        }}>
          <Canvas3D
            polygon={polygon}
            obstacles={obstacles ?? []}
            ceiling={ceiling ?? []}
            bayTypes={bayTypes ?? []}
            placements={visiblePlacements ?? []}
            showLabels={showLabels}
            showCeiling={showCeiling}
            showGaps={showGaps}
            selectedTypeIds={selectedTypeIds}
          />
        </div>
      )}

      {/* Overlay: 2D/3D toggle + Fit button */}
      <div style={{ position: 'absolute', top: 10, right: 10, display: 'flex', gap: 6 }}>
        <div style={{ display: 'flex', background: 'var(--color-card)', border: '1px solid var(--color-border)', borderRadius: 6, overflow: 'hidden' }}>
          {(['2d', '3d'] as const).map(mode => (
            <button
              key={mode}
              onClick={() => { setViewMode(mode); onViewModeChange?.(mode) }}
              style={{
                background: viewMode === mode ? 'var(--color-border)' : 'none',
                border: 'none',
                color: viewMode === mode ? 'var(--color-fg)' : 'var(--color-muted)',
                fontFamily: 'var(--font-mono)',
                fontSize: 11,
                padding: '5px 14px',
                cursor: 'pointer',
              }}
            >
              {mode === '2d' ? '2D' : '3D'}
            </button>
          ))}
        </div>
        {viewMode === '2d' && (
          <button
            onClick={fitToScreen}
            title="Fit to screen"
            style={{
              background: 'var(--color-card)',
              border: '1px solid var(--color-border)',
              borderRadius: 6,
              color: 'var(--color-muted)',
              fontFamily: 'var(--font-mono)',
              fontSize: 11,
              padding: '5px 10px',
              cursor: 'pointer',
            }}
          >
            ⊡ Fit
          </button>
        )}
      </div>

      {/* 3D hint */}
      {viewMode === '3d' && (
        <div style={{
          position: 'absolute', bottom: 10, left: '50%', transform: 'translateX(-50%)',
          background: 'var(--color-card)', border: '1px solid var(--color-border)',
          borderRadius: 6, padding: '4px 12px',
          fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--color-muted)',
          pointerEvents: 'none',
        }}>
          drag to orbit · right-drag to pan · scroll to zoom
        </div>
      )}

      {/* Keyboard shortcuts hint */}
      {viewMode === '2d' && (
        <div style={{
          position: 'absolute', bottom: 10, left: '50%', transform: 'translateX(-50%)',
          background: 'var(--color-card)', border: '1px solid var(--color-border)',
          borderRadius: 6, padding: '4px 12px',
          fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--color-muted)',
          pointerEvents: 'none', display: 'flex', gap: 12,
        }}>
          {[['R', 'fit'], ['L', 'labels'], ['G', 'gaps'], ['C', 'ceiling'], ['2/3', 'view']].map(([k, v]) => (
            <span key={k}><span style={{ color: 'var(--color-fg)' }}>{k}</span> {v}</span>
          ))}
        </div>
      )}

      {/* Tooltip */}
      {tooltip && (
        <div style={{
          position: 'absolute',
          left: tooltip.tx,
          top: tooltip.ty,
          background: 'var(--color-card)',
          border: '1px solid var(--color-border)',
          borderRadius: 6,
          padding: '8px 10px',
          fontFamily: 'var(--font-mono)',
          fontSize: 11,
          pointerEvents: 'none',
          zIndex: 10,
          minWidth: 180,
        }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 6 }}>
            <div style={{ width: 8, height: 8, borderRadius: 2, background: tooltip.color, flexShrink: 0 }} />
            <span style={{ color: 'var(--color-fg)', fontWeight: 700 }}>Type {tooltip.type.id}</span>
          </div>
          {([
            ['SIZE', `${tooltip.dims.w} × ${tooltip.dims.d} mm`],
            ['HEIGHT', `${tooltip.type.h} mm`],
            ['GAP', `${tooltip.type.gap} mm`],
            ['LOADS', String(tooltip.type.loads)],
            ['PRICE', String(tooltip.type.price)],
          ] as [string, string][]).map(([label, value]) => (
            <div key={label} style={{ display: 'flex', justifyContent: 'space-between', gap: 16, marginBottom: 2 }}>
              <span style={{ color: 'var(--color-muted)' }}>{label}</span>
              <span style={{ color: 'var(--color-fg)' }}>{value}</span>
            </div>
          ))}
          <div style={{ display: 'flex', justifyContent: 'space-between', gap: 16, marginTop: 4 }}>
            <span style={{ color: 'var(--color-muted)' }}>Q RATIO</span>
            <span style={{
              color: tooltip.isBest ? 'var(--color-accent)' : tooltip.isWorst ? 'var(--color-destructive)' : 'var(--color-fg)',
              fontWeight: 600,
            }}>
              {tooltip.ratio.toFixed(2)}{tooltip.isBest ? ' ★' : tooltip.isWorst ? ' ↓' : ''}
            </span>
          </div>
        </div>
      )}

      {/* Empty state */}
      {!polygon && (
        <div style={{
          position: 'absolute', inset: 0,
          display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center',
          pointerEvents: 'none', gap: 8,
        }}>
          <span style={{ color: 'var(--color-muted)', fontSize: 14 }}>No warehouse loaded</span>
          <span style={{ color: 'var(--color-border)', fontSize: 12 }}>Load the 4 CSV files in the left panel</span>
        </div>
      )}
    </div>
  )
}
