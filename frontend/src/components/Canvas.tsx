import { useRef, useState, useEffect, useCallback } from 'react'
import { useCanvas, type Transform } from '../hooks/useCanvas'
import { getBayColor } from './BayLegend'
import { bayDimensions, polygonBounds } from '../lib/geometry'
import type { Point, Obstacle, CeilingSegment, BayType, PlacedBay } from '../types'

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

function hexRgb(hex: string): [number, number, number] {
  return [parseInt(hex.slice(1, 3), 16), parseInt(hex.slice(3, 5), 16), parseInt(hex.slice(5, 7), 16)]
}

function shadeHex(hex: string, f: number): string {
  const [r, g, b] = hexRgb(hex)
  return `rgba(${Math.round(r * f)},${Math.round(g * f)},${Math.round(b * f)},1)`
}

function polyPath(ctx: CanvasRenderingContext2D, pts: Array<{ x: number; y: number }>) {
  ctx.beginPath()
  ctx.moveTo(pts[0].x, pts[0].y)
  for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x, pts[i].y)
  ctx.closePath()
}

// ─── 2D drawing ────────────────────────────────────────────────────────────

function draw2D(
  ctx: CanvasRenderingContext2D, _W: number, H: number, t: Transform,
  polygon: Point[], obstacles: Obstacle[], ceiling: CeilingSegment[],
  bayTypes: BayType[], placements: PlacedBay[],
  showGaps: boolean, showCeiling: boolean, showLabels: boolean,
  hoveredIdx: number | null,
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
  ctx.fillStyle = '#08111f'
  ctx.fill()
  ctx.strokeStyle = '#334155'
  ctx.lineWidth = 1.5
  ctx.stroke()

  // Ceiling overlay
  if (showCeiling && ceiling.length > 0) {
    const sorted = [...ceiling].sort((a, b) => a.x - b.x)
    const maxH = Math.max(...sorted.map(s => s.h))
    const minH = Math.min(...sorted.map(s => s.h))

    for (let i = 0; i < sorted.length; i++) {
      const seg = sorted[i]
      const nextX = i + 1 < sorted.length ? sorted[i + 1].x : bounds.maxX
      const alpha = (1 - seg.h / maxH) * 0.38
      ctx.fillStyle = `rgba(0,0,0,${alpha.toFixed(3)})`
      ctx.fillRect(t.offsetX + seg.x * t.scale, 0, (nextX - seg.x) * t.scale, H)
    }

    ctx.beginPath()
    for (let i = 0; i < sorted.length; i++) {
      const seg = sorted[i]
      const nextX = i + 1 < sorted.length ? sorted[i + 1].x : bounds.maxX
      const sx = t.offsetX + seg.x * t.scale
      const ex = t.offsetX + nextX * t.scale
      const sy = t.offsetY - seg.h * t.scale
      if (i === 0) ctx.moveTo(sx, sy); else ctx.lineTo(sx, sy)
      ctx.lineTo(ex, sy)
    }
    ctx.lineTo(t.offsetX + bounds.maxX * t.scale, 0)
    ctx.lineTo(t.offsetX + sorted[0].x * t.scale, 0)
    ctx.closePath()
    ctx.fillStyle = 'rgba(239,68,68,0.07)'
    ctx.fill()

    ctx.strokeStyle = '#ef4444'
    ctx.lineWidth = 1.5
    ctx.setLineDash([])
    ctx.beginPath()
    for (let i = 0; i < sorted.length; i++) {
      const seg = sorted[i]
      const nextX = i + 1 < sorted.length ? sorted[i + 1].x : bounds.maxX
      const sx = t.offsetX + seg.x * t.scale
      const ex = t.offsetX + nextX * t.scale
      const sy = t.offsetY - seg.h * t.scale
      if (i === 0) ctx.moveTo(sx, sy); else ctx.lineTo(sx, sy)
      ctx.lineTo(ex, sy)
    }
    ctx.stroke()

    for (let i = 0; i < sorted.length; i++) {
      const seg = sorted[i]
      const nextX = i + 1 < sorted.length ? sorted[i + 1].x : bounds.maxX
      const color = seg.h >= maxH ? '#22c55e' : seg.h <= minH ? '#ef4444' : '#eab308'
      const lx = t.offsetX + (seg.x + (nextX - seg.x) / 2) * t.scale
      ctx.fillStyle = color
      ctx.font = '8px monospace'
      ctx.textAlign = 'center'
      ctx.textBaseline = 'top'
      ctx.fillText(`${seg.h}mm`, lx, 6)
    }
  }

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
      if (p.rotation === 0) {
        const gy = t.offsetY - (p.y + dims.d + type.gap) * t.scale
        const gh = type.gap * t.scale
        ctx.fillStyle = color + '1f'
        ctx.fillRect(sx, gy, sw, gh)
        ctx.strokeStyle = color + '8c'
        ctx.lineWidth = 0.8
        ctx.setLineDash([3, 2])
        ctx.strokeRect(sx, gy, sw, gh)
        ctx.setLineDash([])
      } else {
        const gx = t.offsetX + (p.x + dims.w) * t.scale
        const gw = type.gap * t.scale
        ctx.fillStyle = color + '1f'
        ctx.fillRect(gx, sy, gw, sh)
        ctx.strokeStyle = color + '8c'
        ctx.lineWidth = 0.8
        ctx.setLineDash([3, 2])
        ctx.strokeRect(gx, sy, gw, sh)
        ctx.setLineDash([])
      }
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

// ─── 3D drawing with rotatable orthographic projection ─────────────────────
//
// azimuth: rotation around the vertical axis (left/right)
// elevation: tilt angle (up/down)
//
// Projection:
//   rx = wx*cos(az) - wy*sin(az)
//   ry = wx*sin(az) + wy*cos(az)
//   screenX = ox + rx * s
//   screenY = oy - (ry*sin(el) + wz*cos(el)) * s
//
// Eye direction (toward viewer): (-sin(az)*cos(el), -cos(az)*cos(el), sin(el))
// Face visibility:
//   -X face (x=bx):   visible when sin(az) > 0
//   +X face (x=bx+bw): visible when sin(az) < 0
//   -Y face (y=by):   visible when cos(az) > 0
//   +Y face (y=by+bd): visible when cos(az) < 0
//   Top (z=bh):       always visible

function draw3D(
  ctx: CanvasRenderingContext2D, W: number, H: number,
  polygon: Point[], obstacles: Obstacle[], ceiling: CeilingSegment[],
  bayTypes: BayType[], placements: PlacedBay[], showLabels: boolean,
  azimuth: number, elevation: number,
) {
  const typeMap = new Map(bayTypes.map(bt => [bt.id, bt]))
  const bounds = polygonBounds(polygon)
  const maxCeilH = ceiling.length > 0
    ? Math.max(...ceiling.map(seg => seg.h))
    : Math.max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY) * 0.4
  const padding = 32

  const cosAz = Math.cos(azimuth)
  const sinAz = Math.sin(azimuth)
  const sinEl = Math.sin(elevation)
  const cosEl = Math.cos(elevation)

  function rawProj(wx: number, wy: number, wz: number) {
    const rx = wx * cosAz - wy * sinAz
    const ry = wx * sinAz + wy * cosAz
    return { px: rx, py: ry * sinEl + wz * cosEl }
  }

  // Fit the warehouse + ceiling into the canvas
  const corners: [number, number, number][] = [
    [bounds.minX, bounds.minY, 0], [bounds.maxX, bounds.minY, 0],
    [bounds.minX, bounds.maxY, 0], [bounds.maxX, bounds.maxY, 0],
    [bounds.minX, bounds.minY, maxCeilH], [bounds.maxX, bounds.minY, maxCeilH],
    [bounds.minX, bounds.maxY, maxCeilH], [bounds.maxX, bounds.maxY, maxCeilH],
  ]
  const projs = corners.map(([wx, wy, wz]) => rawProj(wx, wy, wz))
  const pxMin = Math.min(...projs.map(p => p.px))
  const pxMax = Math.max(...projs.map(p => p.px))
  const pyMin = Math.min(...projs.map(p => p.py))
  const pyMax = Math.max(...projs.map(p => p.py))

  const projW = pxMax - pxMin || 1
  const projH = pyMax - pyMin || 1
  const s = Math.min((W - 2 * padding) / projW, (H - 2 * padding) / projH)

  // Center in canvas: (pxMin → padding), (pyMax → padding on screen top)
  const ox = padding + (W - 2 * padding - projW * s) / 2 - pxMin * s
  const oy = H - padding - (H - 2 * padding - projH * s) / 2 + pyMin * s

  function isoP(wx: number, wy: number, wz: number) {
    const { px, py } = rawProj(wx, wy, wz)
    return { x: ox + px * s, y: oy - py * s }
  }

  // Depth = "into screen" distance — used for painter's algorithm
  function depth(wx: number, wy: number) {
    return wx * sinAz + wy * cosAz
  }

  function drawFace(pts: Array<{ x: number; y: number }>, fill: string, stroke: string, lw = 0.5) {
    polyPath(ctx, pts)
    ctx.fillStyle = fill; ctx.fill()
    ctx.strokeStyle = stroke; ctx.lineWidth = lw; ctx.stroke()
  }

  // Floor
  const floorPts = polygon.map(v => isoP(v.x, v.y, 0))
  polyPath(ctx, floorPts)
  ctx.fillStyle = '#08111f'; ctx.fill()
  ctx.strokeStyle = '#1e293b'; ctx.lineWidth = 1.2; ctx.stroke()

  // Grid
  ctx.strokeStyle = '#0d1a2e'; ctx.lineWidth = 0.4
  const step = Math.max(500, Math.ceil(Math.max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY) / 20 / 500) * 500)
  for (let x = bounds.minX; x <= bounds.maxX; x += step) {
    const a = isoP(x, bounds.minY, 0), b = isoP(x, bounds.maxY, 0)
    ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke()
  }
  for (let y = bounds.minY; y <= bounds.maxY; y += step) {
    const a = isoP(bounds.minX, y, 0), b = isoP(bounds.maxX, y, 0)
    ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke()
  }

  // Merge obstacles + bays into one list, sort back-to-front together
  const obsHeight = maxCeilH * 0.5
  const edgeStroke = 'rgba(0,0,0,0.45)'
  const obsEdge = 'rgba(0,0,0,0.5)'

  type DrawItem =
    | { kind: 'bay'; p: PlacedBay; cx: number; cy: number }
    | { kind: 'obs'; o: Obstacle; cx: number; cy: number }

  const items: DrawItem[] = [
    ...placements.map(p => {
      const type = typeMap.get(p.id)
      const dims = type ? bayDimensions(p, type) : { w: 0, d: 0 }
      return { kind: 'bay' as const, p, cx: p.x + dims.w / 2, cy: p.y + dims.d / 2 }
    }),
    ...obstacles.map(o => ({ kind: 'obs' as const, o, cx: o.x + o.w / 2, cy: o.y + o.d / 2 })),
  ]

  items.sort((a, b) => depth(b.cx, b.cy) - depth(a.cx, a.cy))

  for (const item of items) {
    if (item.kind === 'bay') {
      const { p } = item
      const type = typeMap.get(p.id)
      if (!type) continue
      const dims = bayDimensions(p, type)
      const color = getBayColor(p.id)
      const bx = p.x, by = p.y, bw = dims.w, bd = dims.d, bh = type.h

      const xFaceX = sinAz >= 0 ? bx : bx + bw
      const yFaceY = cosAz >= 0 ? by : by + bd
      const xShade = sinAz >= 0 ? 0.68 : 0.60
      const yShade = cosAz >= 0 ? 0.78 : 0.70

      const xFacePts = [isoP(xFaceX, by, 0), isoP(xFaceX, by + bd, 0), isoP(xFaceX, by + bd, bh), isoP(xFaceX, by, bh)]
      const yFacePts = [isoP(bx, yFaceY, 0), isoP(bx + bw, yFaceY, 0), isoP(bx + bw, yFaceY, bh), isoP(bx, yFaceY, bh)]
      const topPts = [isoP(bx, by, bh), isoP(bx + bw, by, bh), isoP(bx + bw, by + bd, bh), isoP(bx, by + bd, bh)]

      const xd = depth(xFaceX, by + bd / 2)
      const yd = depth(bx + bw / 2, yFaceY)
      if (xd >= yd) {
        drawFace(xFacePts, shadeHex(color, xShade), edgeStroke)
        drawFace(yFacePts, shadeHex(color, yShade), edgeStroke)
      } else {
        drawFace(yFacePts, shadeHex(color, yShade), edgeStroke)
        drawFace(xFacePts, shadeHex(color, xShade), edgeStroke)
      }
      drawFace(topPts, shadeHex(color, 1.0), 'rgba(255,255,255,0.18)', 0.5)

      if (showLabels) {
        const lc = isoP(bx + bw / 2, by + bd / 2, bh)
        ctx.fillStyle = 'rgba(255,255,255,0.92)'
        ctx.font = 'bold 8px monospace'
        ctx.textAlign = 'center'
        ctx.textBaseline = 'middle'
        ctx.fillText(String(p.id), lc.x, lc.y)
      }
    } else {
      const { o } = item
      const bx = o.x, by = o.y, bw = o.w, bd = o.d, bh = obsHeight

      const xFaceX = sinAz >= 0 ? bx : bx + bw
      const yFaceY = cosAz >= 0 ? by : by + bd

      const xFacePts = [isoP(xFaceX, by, 0), isoP(xFaceX, by + bd, 0), isoP(xFaceX, by + bd, bh), isoP(xFaceX, by, bh)]
      const yFacePts = [isoP(bx, yFaceY, 0), isoP(bx + bw, yFaceY, 0), isoP(bx + bw, yFaceY, bh), isoP(bx, yFaceY, bh)]
      const topPts = [isoP(bx, by, bh), isoP(bx + bw, by, bh), isoP(bx + bw, by + bd, bh), isoP(bx, by + bd, bh)]

      const xd = depth(xFaceX, by + bd / 2)
      const yd = depth(bx + bw / 2, yFaceY)
      if (xd >= yd) {
        drawFace(xFacePts, 'rgba(185,28,28,0.85)', obsEdge)
        drawFace(yFacePts, 'rgba(220,38,38,0.85)', obsEdge)
      } else {
        drawFace(yFacePts, 'rgba(220,38,38,0.85)', obsEdge)
        drawFace(xFacePts, 'rgba(185,28,28,0.85)', obsEdge)
      }
      drawFace(topPts, 'rgba(239,68,68,0.9)', 'rgba(255,255,255,0.15)', 0.5)

      const lc = isoP(bx + bw / 2, by + bd / 2, bh)
      ctx.fillStyle = 'rgba(255,255,255,0.9)'
      ctx.font = 'bold 8px monospace'
      ctx.textAlign = 'center'
      ctx.textBaseline = 'middle'
      ctx.fillText('OBS', lc.x, lc.y)
    }
  }

  // Ceiling planes
  if (ceiling.length > 0) {
    const cSorted = [...ceiling].sort((a, b) => a.x - b.x)
    const cMaxH = Math.max(...cSorted.map(c => c.h))
    const cMinH = Math.min(...cSorted.map(c => c.h))
    for (let i = 0; i < cSorted.length; i++) {
      const seg = cSorted[i]
      const nextX = i + 1 < cSorted.length ? cSorted[i + 1].x : bounds.maxX
      const fillC = seg.h >= cMaxH ? 'rgba(34,197,94,0.07)' : seg.h <= cMinH ? 'rgba(239,68,68,0.10)' : 'rgba(250,204,21,0.07)'
      const edgeC = seg.h >= cMaxH ? 'rgba(34,197,94,0.75)' : seg.h <= cMinH ? 'rgba(239,68,68,0.85)' : 'rgba(250,204,21,0.75)'
      const pts = [isoP(seg.x, bounds.minY, seg.h), isoP(nextX, bounds.minY, seg.h), isoP(nextX, bounds.maxY, seg.h), isoP(seg.x, bounds.maxY, seg.h)]
      polyPath(ctx, pts)
      ctx.fillStyle = fillC; ctx.fill()
      ctx.strokeStyle = edgeC; ctx.lineWidth = 1.0
      ctx.setLineDash([4, 3]); ctx.stroke(); ctx.setLineDash([])
      const mid = isoP((seg.x + nextX) / 2, bounds.minY, seg.h)
      ctx.fillStyle = edgeC; ctx.font = '8px monospace'
      ctx.textAlign = 'center'; ctx.textBaseline = 'bottom'
      ctx.fillText(`${seg.h}mm`, mid.x, mid.y - 4)
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
  const [iso3d, setIso3d] = useState({ azimuth: Math.PI / 4, elevation: Math.PI / 6, panX: 0, panY: 0, zoom: 1 })
  const [is3dGrabbing, setIs3dGrabbing] = useState(false)
  const autoFitRef = useRef<Point[] | null>(null)
  const viewModeRef = useRef<'2d' | '3d'>('2d')
  const is3dOrbitRef = useRef(false)
  const is3dPanRef = useRef(false)
  const last3dMouse = useRef({ x: 0, y: 0 })

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
      const factor = e.deltaY < 0 ? 1.1 : 0.9
      const rect = canvas.getBoundingClientRect()
      const mx = e.clientX - rect.left
      const my = e.clientY - rect.top
      if (viewModeRef.current === '2d') {
        setTransform(t => {
          const newS = Math.min(10, Math.max(0.05, t.scale * factor))
          return {
            scale: newS,
            offsetX: mx - (mx - t.offsetX) * (newS / t.scale),
            offsetY: my - (my - t.offsetY) * (newS / t.scale),
          }
        })
      } else {
        const cx = rect.width / 2
        const cy = rect.height / 2
        setIso3d(prev => {
          const newZoom = Math.min(8, Math.max(0.1, prev.zoom * factor))
          const r = newZoom / prev.zoom
          return {
            ...prev,
            zoom: newZoom,
            panX: (1 - r) * (mx - cx) + r * prev.panX,
            panY: (1 - r) * (my - cy) + r * prev.panY,
          }
        })
      }
    }
    canvas.addEventListener('wheel', handler, { passive: false })
    return () => canvas.removeEventListener('wheel', handler)
  }, [setTransform])

  // Reset 3D camera when case changes
  useEffect(() => {
    setIso3d({ azimuth: Math.PI / 4, elevation: Math.PI / 6, panX: 0, panY: 0, zoom: 1 })
  }, [polygon])

  // Fit to screen (2D)
  const fitToScreen = useCallback(() => {
    if (!polygon || canvasSize.w === 0) return
    if (viewMode === '3d') {
      setIso3d({ azimuth: Math.PI / 4, elevation: Math.PI / 6, panX: 0, panY: 0, zoom: 1 })
      return
    }
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

  // Draw
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas || canvasSize.w === 0) return
    const dpr = window.devicePixelRatio || 1
    const ctx = canvas.getContext('2d')!
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    ctx.clearRect(0, 0, canvasSize.w, canvasSize.h)

    if (!polygon || !bayTypes) return

    if (viewMode === '2d') {
      draw2D(
        ctx, canvasSize.w, canvasSize.h, transform,
        polygon, obstacles ?? [], ceiling ?? [],
        bayTypes, visiblePlacements ?? [],
        showGaps, showCeiling, showLabels, hoveredIdx,
      )
    } else {
      const cx = canvasSize.w / 2, cy = canvasSize.h / 2
      ctx.save()
      ctx.translate(cx * (1 - iso3d.zoom) + iso3d.panX, cy * (1 - iso3d.zoom) + iso3d.panY)
      ctx.scale(iso3d.zoom, iso3d.zoom)
      draw3D(
        ctx, canvasSize.w, canvasSize.h,
        polygon, obstacles ?? [], ceiling ?? [],
        bayTypes, visiblePlacements ?? [], showLabels,
        iso3d.azimuth, iso3d.elevation,
      )
      ctx.restore()
    }
  }, [canvasSize, transform, viewMode, iso3d, polygon, obstacles, ceiling, bayTypes, visiblePlacements, showGaps, showCeiling, showLabels, hoveredIdx])

  const handleMouseDown = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    if (viewMode === '3d') {
      // left button = orbit, right/middle = pan
      if (e.button === 0) {
        is3dOrbitRef.current = true
      } else {
        is3dPanRef.current = true
      }
      setIs3dGrabbing(true)
      last3dMouse.current = { x: e.clientX, y: e.clientY }
    } else {
      on2dMouseDown(e)
    }
  }, [viewMode, on2dMouseDown])

  const handleMouseUp = useCallback(() => {
    is3dOrbitRef.current = false
    is3dPanRef.current = false
    setIs3dGrabbing(false)
    on2dMouseUp()
  }, [on2dMouseUp])

  const handleMouseMove = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    if (viewMode === '3d') {
      const dx = e.clientX - last3dMouse.current.x
      const dy = e.clientY - last3dMouse.current.y
      last3dMouse.current = { x: e.clientX, y: e.clientY }
      if (is3dOrbitRef.current) {
        setIso3d(prev => ({
          ...prev,
          azimuth: prev.azimuth + dx * 0.006,
          // clamp elevation: just above ground (0.08) to nearly top-down (π/2 - 0.05)
          elevation: Math.max(0.08, Math.min(Math.PI / 2 - 0.05, prev.elevation + dy * 0.006)),
        }))
      } else if (is3dPanRef.current) {
        setIso3d(prev => ({ ...prev, panX: prev.panX + dx, panY: prev.panY + dy }))
      }
      return
    }
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
  }, [viewMode, visiblePlacements, bayTypes, transform, onMouseMovePan])

  const handleMouseLeave = useCallback(() => {
    is3dOrbitRef.current = false
    is3dPanRef.current = false
    setIs3dGrabbing(false)
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
          cursor: viewMode === '3d'
            ? (is3dGrabbing ? 'grabbing' : 'grab')
            : (hoveredIdx !== null ? 'pointer' : 'grab'),
        }}
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={handleMouseLeave}
        onContextMenu={e => { if (viewMode === '3d') e.preventDefault() }}
      />

      {/* Overlay: 2D/3D toggle + Fit button */}
      <div style={{ position: 'absolute', top: 10, right: 10, display: 'flex', gap: 6 }}>
        <div style={{ display: 'flex', background: '#0f172a', border: '1px solid #334155', borderRadius: 6, overflow: 'hidden' }}>
          {(['2d', '3d'] as const).map(mode => (
            <button
              key={mode}
              onClick={() => { setViewMode(mode); onViewModeChange?.(mode) }}
              style={{
                background: viewMode === mode ? '#1e293b' : 'none',
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
        <button
          onClick={fitToScreen}
          title="Fit to screen"
          style={{
            background: '#0f172a',
            border: '1px solid #334155',
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
      </div>

      {/* 3D hint */}
      {viewMode === '3d' && (
        <div style={{
          position: 'absolute', bottom: 10, left: '50%', transform: 'translateX(-50%)',
          background: 'rgba(15,23,42,0.85)', border: '1px solid #334155',
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
          background: 'rgba(15,23,42,0.85)', border: '1px solid #334155',
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
