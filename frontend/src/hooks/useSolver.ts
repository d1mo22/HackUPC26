import { useState } from 'react'
import { parseSolution } from '../lib/csvParser'
import type { RawFiles } from '../components/FileLoader'
import type { Solution } from '../types'

export function useSolver() {
  const [isRunning, setIsRunning] = useState(false)
  const [error, setError] = useState<string | null>(null)

  async function run(rawFiles: RawFiles): Promise<Solution | null> {
    setIsRunning(true)
    setError(null)

    try {
      const form = new FormData()
      form.append('warehouse', rawFiles.warehouse)
      form.append('obstacles', rawFiles.obstacles)
      form.append('ceiling',   rawFiles.ceiling)
      form.append('types',     rawFiles.types)

      const res = await fetch('/solve', { method: 'POST', body: form })

      if (!res.ok) {
        const msg = await res.text()
        throw new Error(msg || `HTTP ${res.status}`)
      }

      const csv = await res.text()
      const blob = new Blob([csv], { type: 'text/csv' })
      const file = new File([blob], 'solution.csv')
      const placements = await parseSolution(file)

      return { placements }
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
      return null
    } finally {
      setIsRunning(false)
    }
  }

  return { run, isRunning, error }
}
