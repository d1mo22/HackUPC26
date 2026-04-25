import type { BayType, CeilingSegment, PlacedBay, Point } from '../types'

/** Effective width/depth of a placed bay after applying rotation */
export function bayDimensions(bay: PlacedBay, type: BayType): { w: number; d: number } {
  return bay.rotation === 0
    ? { w: type.w, d: type.d }
    : { w: type.d, d: type.w }
}

/** Minimum ceiling height over the x-range [x1, x2) */
export function minCeilingBetween(x1: number, x2: number, ceiling: CeilingSegment[]): number {
  const sorted = [...ceiling].sort((a, b) => a.x - b.x)
  let result = Infinity
  for (let i = 0; i < sorted.length; i++) {
    const cx = sorted[i].x
    const nx = i + 1 < sorted.length ? sorted[i + 1].x : Infinity
    if (Math.max(x1, cx) < Math.min(x2, nx)) {
      result = Math.min(result, sorted[i].h)
    }
  }
  return result
}

/** Bounding box of the polygon */
export function polygonBounds(polygon: Point[]): { minX: number; minY: number; maxX: number; maxY: number } {
  const xs = polygon.map((p) => p.x)
  const ys = polygon.map((p) => p.y)
  return { minX: Math.min(...xs), minY: Math.min(...ys), maxX: Math.max(...xs), maxY: Math.max(...ys) }
}
