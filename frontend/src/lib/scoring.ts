import type { BayType, PlacedBay, Point } from '../types'
import { bayDimensions, polygonBounds } from './geometry'

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

  const bounds = polygonBounds(polygon)
  const areaWarehouse = (bounds.maxX - bounds.minX) * (bounds.maxY - bounds.minY)
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
