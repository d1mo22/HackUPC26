import type { BayType, PlacedBay, Point } from '../types'
import { bayDimensions } from './geometry'

export interface Metrics {
  q: number
  areaCovered: number
  areaWarehouse: number
  coveragePct: number
  totalPrice: number
  totalLoads: number
  bayCount: number
}

export function computeMetrics(
  placements: PlacedBay[],
  bayTypes: BayType[],
  polygon: Point[],
): Metrics {
  const typeMap = new Map(bayTypes.map((t) => [t.id, t]))

  let sumPrice = 0
  let sumLoad = 0
  let areaCovered = 0
  let totalPrice = 0
  let totalLoads = 0

  for (const p of placements) {
    const t = typeMap.get(p.id)
    if (!t) continue
    const { w, d } = bayDimensions(p, t)
    sumPrice += t.price 
    sumLoad += t.loads
    areaCovered += w * d
    totalPrice += t.price
    totalLoads += t.loads
  }

  let priceLoad = sumPrice / sumLoad

  // Shoelace formula for the actual polygon area
  let areaWarehouse = 0
  for (let i = 0; i < polygon.length; i++) {
    const j = (i + 1) % polygon.length
    areaWarehouse += polygon[i].x * polygon[j].y
    areaWarehouse -= polygon[j].x * polygon[i].y
  }
  areaWarehouse = Math.abs(areaWarehouse) / 2
  const ratio = areaCovered / areaWarehouse
  const q = placements.length > 0 ? Math.pow(priceLoad, 2 - ratio) : 0

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
