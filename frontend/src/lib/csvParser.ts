import Papa from 'papaparse'
import type { BayType, CeilingSegment, Obstacle, PlacedBay, Point } from '../types'

function parseCSV(file: File): Promise<string[][]> {
  return new Promise((resolve, reject) => {
    Papa.parse<string[]>(file, {
      skipEmptyLines: true,
      complete: (r) => resolve(r.data),
      error: reject,
    })
  })
}

function toInt(s: string): number {
  const v = parseInt(s.trim(), 10)
  if (isNaN(v)) throw new Error(`"${s.trim()}" is not a valid integer`)
  return v
}

// Skip any row whose first cell is non-numeric (header rows)
function skipHeaders(rows: string[][]): string[][] {
  return rows.filter(row => row.length > 0 && !isNaN(parseInt(row[0]?.trim(), 10)))
}
function validate(rows: string[][], expectedCols: number, name: string, minRows = 1, colNames?: string[]) {
  const colHint = colNames ? ` (${colNames.join(', ')})` : ''
  if (rows.length < minRows) {
    throw new Error(`Expected at least ${minRows} row${minRows !== 1 ? 's' : ''}, found ${rows.length}`)
  }
  for (let i = 0; i < rows.length; i++) {
    const row = rows[i]
    if (row.length !== expectedCols) {
      if (i === 0) throw new Error(`Wrong file? Expected columns: ${colNames ? colNames.join(', ') : `${expectedCols}`}`)
      throw new Error(`Row ${i + 1}: expected ${expectedCols} columns${colHint}, got ${row.length}`)
    }
    for (let j = 0; j < row.length; j++) {
      const cell = row[j]
      if (isNaN(parseInt(cell.trim(), 10))) {
        const col = colNames?.[j] ? `"${colNames[j]}"` : `column ${j + 1}`
        throw new Error(`Row ${i + 1}: ${col} is not a number — got "${cell.trim()}"`)
      }
    }
  }
}

export async function parseWarehouse(file: File): Promise<Point[]> {
  const rows = skipHeaders(await parseCSV(file))
  validate(rows, 2, 'warehouse.csv', 3, ['x', 'y'])
  return rows.map(([x, y]) => ({ x: toInt(x), y: toInt(y) }))
}

export async function parseObstacles(file: File): Promise<Obstacle[]> {
  const rows = skipHeaders(await parseCSV(file))
  if (rows.length > 0) validate(rows, 4, 'obstacles.csv', 1, ['x', 'y', 'w', 'd'])
  return rows.map(([x, y, w, d]) => ({ x: toInt(x), y: toInt(y), w: toInt(w), d: toInt(d) }))
}

export async function parseCeiling(file: File): Promise<CeilingSegment[]> {
  const rows = skipHeaders(await parseCSV(file))
  validate(rows, 2, 'ceiling.csv', 1, ['x', 'h'])
  return rows.map(([x, h]) => ({ x: toInt(x), h: toInt(h) }))
}

export async function parseBayTypes(file: File): Promise<BayType[]> {
  const rows = skipHeaders(await parseCSV(file))
  validate(rows, 7, 'types_of_bays.csv', 1, ['id', 'w', 'd', 'h', 'gap', 'loads', 'price'])
  return rows.map(([id, w, d, h, gap, loads, price]) => ({
    id: toInt(id), w: toInt(w), d: toInt(d), h: toInt(h),
    gap: toInt(gap), loads: toInt(loads), price: toInt(price),
  }))
}

/**
 * Normalise a solver bay (origin x,y at rotation pivot, angle in degrees) into
 * the frontend's axis-aligned representation:
 *   rotation=0 → w×d footprint, gap goes in +Y (above)
 *   rotation=1 → d×w footprint, gap goes in +X (right)
 *
 * The solver rotates the bay rectangle around (x,y) with the standard
 * counter-clockwise rotation matrix applied to local corners {(0,0),(w,0),(w,d),(0,d)}.
 * We compute the AABB min-corner and pick rotation=0/1 based on whether the
 * footprint is w×d or d×w after rotation.
 */
function normaliseBay(id: number, x: number, y: number, w: number, d: number, angleDeg: number): PlacedBay {
  const a = ((angleDeg % 360) + 360) % 360 as 0 | 90 | 180 | 270
  const rad = a * Math.PI / 180
  const ca = Math.cos(rad), sa = Math.sin(rad)
  const locals = [[0, 0], [w, 0], [w, d], [0, d]]
  let minX = Infinity, minY = Infinity
  for (const [lx, ly] of locals) {
    minX = Math.min(minX, x + lx * ca - ly * sa)
    minY = Math.min(minY, y + lx * sa + ly * ca)
  }
  return {
    id,
    x: Math.round(minX),
    y: Math.round(minY),
    rotation: a,
  }
}

export async function parseSolution(file: File, bayTypes?: { id: number; w: number; d: number }[]): Promise<PlacedBay[]> {
  const rows = skipHeaders(await parseCSV(file))
  validate(rows, 4, 'solution.csv', 1, ['Id', 'X', 'Y', 'Rotation'])
  return rows.map(([id, x, y, rotation]) => {
    const bayId = toInt(id)
    const angleDeg = toInt(rotation)
    // If rotation is already 0/1 (legacy format), map to angle: 0→0°, 1→90°.
    if (angleDeg === 0 || angleDeg === 1) {
      return { id: bayId, x: toInt(x), y: toInt(y), rotation: (angleDeg === 0 ? 0 : 90) as 0 | 90 }
    }
    // Rotation is in degrees — need bay dimensions to compute AABB.
    const typeInfo = bayTypes?.find(t => t.id === bayId)
    if (!typeInfo) {
      // Fallback: treat non-zero as 90° (swapped), keep coords as-is.
      return { id: bayId, x: toInt(x), y: toInt(y), rotation: 90 as const }
    }
    return normaliseBay(bayId, toInt(x), toInt(y), typeInfo.w, typeInfo.d, angleDeg)
  })
}
