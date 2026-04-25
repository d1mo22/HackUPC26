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
  x: number        // AABB bottom-left x (mm)
  y: number        // AABB bottom-left y (mm)
  rotation: 0 | 90 | 180 | 270  // normalised angle; 90/270 swap w↔d; gap direction follows angle
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

export interface RunRecord {
  id: number
  solution: Solution
  metrics: {
    q: number
    coveragePct: number
    bayCount: number
  }
  elapsedMs: number
  timestamp: Date
}
