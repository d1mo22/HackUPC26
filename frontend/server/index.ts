import express from 'express'
import multer from 'multer'
import cors from 'cors'
import { execFile } from 'child_process'
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'fs'
import { tmpdir } from 'os'
import { join, dirname } from 'path'
import { fileURLToPath } from 'url'

const __dirname = dirname(fileURLToPath(import.meta.url))

const app = express()
const upload = multer({ storage: multer.memoryStorage() })

app.use(cors())

app.post(
  '/solve',
  upload.fields([
    { name: 'warehouse', maxCount: 1 },
    { name: 'obstacles', maxCount: 1 },
    { name: 'ceiling',   maxCount: 1 },
    { name: 'types',     maxCount: 1 },
  ]),
  (req, res) => {
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
      writeFileSync(join(tmpDir, 'warehouse.csv'),      files.warehouse[0].buffer)
      writeFileSync(join(tmpDir, 'obstacles.csv'),      files.obstacles[0].buffer)
      writeFileSync(join(tmpDir, 'ceiling.csv'),        files.ceiling[0].buffer)
      writeFileSync(join(tmpDir, 'types_of_bays.csv'),  files.types[0].buffer)

      const scriptPath = join(__dirname, 'run_single.py')

      execFile('python3', [scriptPath, tmpDir], { timeout: 35_000 }, (err, stdout, stderr) => {
        if (err) {
          console.error('Solver error:', stderr)
          res.status(500).send(stderr || err.message)
          rmSync(tmpDir, { recursive: true, force: true })
          return
        }

        try {
          const csv = readFileSync(join(tmpDir, 'solution.csv'), 'utf-8')
          res.setHeader('Content-Type', 'text/csv')
          res.send(csv)
        } catch {
          res.status(500).send('Solver did not produce output')
        } finally {
          rmSync(tmpDir, { recursive: true, force: true })
        }
      })
    } catch (e) {
      rmSync(tmpDir, { recursive: true, force: true })
      res.status(500).send(String(e))
    }
  }
)

app.listen(3001, () => console.log('Solver bridge running on http://localhost:3001'))
