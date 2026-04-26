import express from 'express'
import multer from 'multer'
import cors from 'cors'
import { execFile, execFileSync } from 'child_process'
import { mkdtempSync, readFileSync, rmSync, writeFileSync, existsSync } from 'fs'
import { tmpdir } from 'os'
import { join, dirname } from 'path'
import { fileURLToPath } from 'url'

const __dirname = dirname(fileURLToPath(import.meta.url))
const SOLVER_DIR = join(__dirname, '../../solver')
const SOLVER_BIN = join(SOLVER_DIR, 'solver')
const GREEDY_BIN = join(SOLVER_DIR, 'greedy')

// Build both binaries on startup
try {
  console.log('Building solver...')
  execFileSync('make', ['-C', SOLVER_DIR], { stdio: 'inherit' })
  console.log('Solver ready:', SOLVER_BIN)
} catch (e) {
  console.error('Failed to build solver:', e)
  process.exit(1)
}

const app = express()
const upload = multer({ storage: multer.memoryStorage() })

app.use(cors())

function parseQ(csv: string): number {
  // Parse solution CSV and extract Q — but we don't have warehouse/obstacle
  // area here, so we just count bays/price/loads from the CSV and can't
  // compute Q without the area. Instead we rely on the solver printing Q
  // in its stdout and parse that.
  return Infinity
}

function runBinary(bin: string, args: string[], timeout: number): Promise<{ stdout: string; stderr: string; code: number }> {
  return new Promise(resolve => {
    execFile(bin, args, { timeout }, (err, stdout, stderr) => {
      resolve({ stdout, stderr, code: err?.code ?? 0 })
    })
  })
}

function extractQ(stdout: string): number {
  // solver prints "Q=1234.56", greedy prints "Quality: 1234.56"
  const m = stdout.match(/Q=([\d.]+)/) ?? stdout.match(/Quality:\s*([\d.]+)/)
  return m ? parseFloat(m[1]) : Infinity
}

app.post(
  '/solve',
  upload.fields([
    { name: 'warehouse', maxCount: 1 },
    { name: 'obstacles', maxCount: 1 },
    { name: 'ceiling',   maxCount: 1 },
    { name: 'types',     maxCount: 1 },
  ]),
  async (req, res) => {
    const files = req.files as Record<string, Express.Multer.File[]>

    const required = ['warehouse', 'obstacles', 'ceiling', 'types']
    for (const name of required) {
      if (!files[name]?.[0]) {
        res.status(400).send(`Missing file: ${name}`)
        return
      }
    }

    const tmpDir = mkdtempSync(join(tmpdir(), 'warehouse-'))

    try {
      writeFileSync(join(tmpDir, 'warehouse.csv'),     files.warehouse[0].buffer)
      writeFileSync(join(tmpDir, 'obstacles.csv'),     files.obstacles[0].buffer)
      writeFileSync(join(tmpDir, 'ceiling.csv'),       files.ceiling[0].buffer)
      writeFileSync(join(tmpDir, 'types_of_bays.csv'), files.types[0].buffer)

      // Run solver and greedy in parallel
      const [solverResult, greedyResult] = await Promise.all([
        runBinary(SOLVER_BIN, [tmpDir], 120_000),
        runBinary(GREEDY_BIN, [tmpDir], 120_000),
      ])

      console.log('[solver]', solverResult.stdout.trim())
      if (solverResult.stderr) console.error('[solver stderr]', solverResult.stderr)
      console.log('[greedy]', greedyResult.stdout.trim())
      if (greedyResult.stderr) console.error('[greedy stderr]', greedyResult.stderr)

      const solverQ = extractQ(solverResult.stdout)
      const greedyQ = extractQ(greedyResult.stdout)

      console.log(`Q comparison — solver: ${solverQ}  greedy: ${greedyQ}`)

      const solverSolution = join(tmpDir, 'solution.csv')
      const greedySolution = join(tmpDir, 'solution_greedy.csv')

      // Pick the solution with lower Q
      let chosenPath: string
      if (existsSync(greedySolution) && greedyQ < solverQ) {
        chosenPath = greedySolution
        console.log('Winner: greedy')
      } else if (existsSync(solverSolution)) {
        chosenPath = solverSolution
        console.log('Winner: solver')
      } else if (existsSync(greedySolution)) {
        chosenPath = greedySolution
        console.log('Winner: greedy (solver produced no output)')
      } else {
        res.status(500).send('Neither solver nor greedy produced output')
        rmSync(tmpDir, { recursive: true, force: true })
        return
      }

      try {
        const csv = readFileSync(chosenPath, 'utf-8')
        res.setHeader('Content-Type', 'text/csv')
        res.send(csv)
      } catch (e) {
        res.status(500).send('Failed to read solution')
      }

      rmSync(tmpDir, { recursive: true, force: true })
    } catch (e) {
      rmSync(tmpDir, { recursive: true, force: true })
      res.status(500).send(String(e))
    }
  }
)

app.listen(3001, () => console.log('Solver bridge running on http://localhost:3001'))
