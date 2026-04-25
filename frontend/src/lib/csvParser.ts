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

function validate(rows: string[][], expectedCols: number, name: string, minRows = 1) {
  if (rows.length < minRows) {
    throw new Error(`${name}: expected at least ${minRows} row(s), got ${rows.length}`)
  }
  for (let i = 0; i < rows.length; i++) {
    const row = rows[i]
    if (row.length !== expectedCols) {
      throw new Error(
        `${name} row ${i + 1}: expected ${expectedCols} column(s), got ${row.length}`
      )
    }
    for (const cell of row) {
      if (isNaN(parseInt(cell.trim(), 10))) {
        throw new Error(`${name} row ${i + 1}: non-numeric value "${cell.trim()}"`)
      }
    }
  }
}

export async function parseWarehouse(file: File): Promise<Point[]> {
  const rows = await parseCSV(file)
  validate(rows, 2, 'warehouse.csv', 3)
  return rows.map(([x, y]) => ({ x: toInt(x), y: toInt(y) }))
}

export async function parseObstacles(file: File): Promise<Obstacle[]> {
  const rows = await parseCSV(file)
  if (rows.length > 0) validate(rows, 4, 'obstacles.csv')
  return rows.map(([x, y, w, d]) => ({ x: toInt(x), y: toInt(y), w: toInt(w), d: toInt(d) }))
}

export async function parseCeiling(file: File): Promise<CeilingSegment[]> {
  const rows = await parseCSV(file)
  validate(rows, 2, 'ceiling.csv', 1)
  return rows.map(([x, h]) => ({ x: toInt(x), h: toInt(h) }))
}

export async function parseBayTypes(file: File): Promise<BayType[]> {
  const rows = await parseCSV(file)
  validate(rows, 7, 'types_of_bays.csv', 1)
  return rows.map(([id, w, d, h, gap, loads, price]) => ({
    id: toInt(id), w: toInt(w), d: toInt(d), h: toInt(h),
    gap: toInt(gap), loads: toInt(loads), price: toInt(price),
  }))
}

export async function parseSolution(file: File): Promise<PlacedBay[]> {
  const rows = await parseCSV(file)
  const dataRows = rows.filter(([id]) => id.trim().toLowerCase() !== 'id')
  validate(dataRows, 4, 'solution.csv', 1)
  return dataRows.map(([id, x, y, rotation]) => ({
    id: toInt(id), x: toInt(x), y: toInt(y), rotation: toInt(rotation),
  }))
}
