import type { BayType, Obstacle, PlacedBay, Point } from '../types'
import { bayDimensions } from './geometry'

export interface Metrics {
  q: number
  areaCovered: number
  areaWarehouse: number  // available area = polygon - obstacles, matches solver
  coveragePct: number
  totalPrice: number
  totalLoads: number
  bayCount: number
}

/**
 * Mirrors `quality()` in solver/solver.cpp:847
 *   Q = (Σprice / Σloads) ^ (2 - bay_area / available_warehouse_area)
 * where available_warehouse_area = polygon_area - Σ obstacle areas
 * (see solver.cpp:914-918, called at solver.cpp:2368).
 *
 * `obstacles` is optional for backwards-compat with old callers, but should
 * always be provided to match the solver's reported Q.
 */
export function computeMetrics(
  placements: PlacedBay[],
  bayTypes: BayType[],
  polygon: Point[],
  obstacles: Obstacle[] = [],
): Metrics {
  const typeMap = new Map(bayTypes.map((t) => [t.id, t]))

  let totalPrice = 0
  let totalLoads = 0
  let areaCovered = 0

  for (const p of placements) {
    const t = typeMap.get(p.id)
    if (!t) continue
    const { w, d } = bayDimensions(p, t)
    totalPrice += t.price
    totalLoads += t.loads
    areaCovered += w * d
  }

  // Shoelace for the warehouse polygon
  let polygonArea = 0
  for (let i = 0; i < polygon.length; i++) {
    const j = (i + 1) % polygon.length
    polygonArea += polygon[i].x * polygon[j].y
    polygonArea -= polygon[j].x * polygon[i].y
  }
  polygonArea = Math.abs(polygonArea) / 2

  const obstacleArea = obstacles.reduce((acc, o) => acc + o.w * o.d, 0)
  const areaWarehouse = Math.max(1, polygonArea - obstacleArea)

  const ratio = areaCovered / areaWarehouse
  // Match solver: max(1.0, total_loads) guard, empty solution returns 0 in UI.
  const base = totalPrice / Math.max(1, totalLoads)
  const q = placements.length > 0 ? Math.pow(base, 2 - ratio) : 0

  return {
    q,
    areaCovered,
    areaWarehouse,
    coveragePct: ratio * 100,
    totalPrice,
    totalLoads,
    bayCount: placements.length,
  }
}
