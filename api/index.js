const express = require('express');
const multer = require('multer');
const cors = require('cors');
const { execFile } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');

const app = express();
const PORT = 3001;

app.use(cors());
app.use(express.json());

const upload = multer({ storage: multer.memoryStorage() });

const SOLVER_BIN = path.join(__dirname, '..', 'solver', 'solver');

app.post('/solve', upload.fields([
  { name: 'warehouse',  maxCount: 1 },
  { name: 'obstacles',  maxCount: 1 },
  { name: 'ceiling',    maxCount: 1 },
  { name: 'types',      maxCount: 1 },
]), (req, res) => {
  const files = req.files;

  for (const field of ['warehouse', 'obstacles', 'ceiling', 'types']) {
    if (!files?.[field]?.[0]) {
      return res.status(400).json({ error: `Missing file: ${field}` });
    }
  }

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'warehouse-'));

  try {
    fs.writeFileSync(path.join(tmpDir, 'warehouse.csv'),     files.warehouse[0].buffer);
    fs.writeFileSync(path.join(tmpDir, 'obstacles.csv'),     files.obstacles[0].buffer);
    fs.writeFileSync(path.join(tmpDir, 'ceiling.csv'),       files.ceiling[0].buffer);
    fs.writeFileSync(path.join(tmpDir, 'types_of_bays.csv'), files.types[0].buffer);

    execFile(SOLVER_BIN, [tmpDir], { timeout: 35_000 }, (err, stdout, stderr) => {
      if (err) {
        console.error('Solver error:', stderr);
        res.status(500).send(stderr || err.message);
        fs.rmSync(tmpDir, { recursive: true, force: true });
        return;
      }

      const solutionPath = path.join(tmpDir, 'solution.csv');
      if (!fs.existsSync(solutionPath)) {
        res.status(500).send('Solver did not produce solution.csv');
        fs.rmSync(tmpDir, { recursive: true, force: true });
        return;
      }

      const csv = fs.readFileSync(solutionPath, 'utf-8');
      res.setHeader('Content-Type', 'text/csv');
      res.send(csv);
      fs.rmSync(tmpDir, { recursive: true, force: true });
    });
  } catch (e) {
    fs.rmSync(tmpDir, { recursive: true, force: true });
    res.status(500).send(String(e));
  }
});

app.get('/health', (req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

app.listen(PORT, () => {
  console.log(`Solver API running on http://localhost:${PORT}`);
});
