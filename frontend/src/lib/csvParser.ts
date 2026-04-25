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

function toInt(s: string) {
  return parseInt(s.trim(), 10)
}

export async function parseWarehouse(file: File): Promise<Point[]> {
  const rows = await parseCSV(file)
  return rows.map(([x, y]) => ({ x: toInt(x), y: toInt(y) }))
}

export async function parseObstacles(file: File): Promise<Obstacle[]> {
  const rows = await parseCSV(file)
  return rows.map(([x, y, w, d]) => ({ x: toInt(x), y: toInt(y), w: toInt(w), d: toInt(d) }))
}

export async function parseCeiling(file: File): Promise<CeilingSegment[]> {
  const rows = await parseCSV(file)
  return rows.map(([x, h]) => ({ x: toInt(x), h: toInt(h) }))
}

export async function parseBayTypes(file: File): Promise<BayType[]> {
  const rows = await parseCSV(file)
  return rows.map(([id, w, d, h, gap, loads, price]) => ({
    id: toInt(id), w: toInt(w), d: toInt(d), h: toInt(h),
    gap: toInt(gap), loads: toInt(loads), price: toInt(price),
  }))
}

export async function parseSolution(file: File): Promise<PlacedBay[]> {
  const rows = await parseCSV(file)
  return rows
    .filter(([id]) => id.trim().toLowerCase() !== 'id')
    .map(([id, x, y, rotation]) => ({
      id: toInt(id), x: toInt(x), y: toInt(y), rotation: toInt(rotation),
    }))
}
