export interface Point {
  x: number
  y: number
}

export interface Obstacle {
  x: number
  y: number
  w: number
  d: number
}

/** One row from ceiling.csv — segment [x, nextX) has height h */
export interface CeilingSegment {
  x: number
  h: number
}

/** One row from types_of_bays.csv */
export interface BayType {
  id: number
  w: number
  d: number
  h: number
  gap: number
  loads: number
  price: number
}

/** One row from solution.csv */
export interface PlacedBay {
  id: number       // references BayType.id
  x: number
  y: number
  rotation: number // 0 or 1 (1 = w/d swapped)
}

/** All data needed to render a case */
export interface WarehouseCase {
  polygon: Point[]
  obstacles: Obstacle[]
  ceiling: CeilingSegment[]
  bayTypes: BayType[]
}

export interface Solution {
  placements: PlacedBay[]
}
