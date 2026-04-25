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

export async function parseSolution(file: File): Promise<PlacedBay[]> {
  const rows = skipHeaders(await parseCSV(file))
  validate(rows, 4, 'solution.csv', 1, ['Id', 'X', 'Y', 'Rotation'])
  return rows.map(([id, x, y, rotation]) => ({
    id: toInt(id), x: toInt(x), y: toInt(y), rotation: toInt(rotation),
  }))
}
