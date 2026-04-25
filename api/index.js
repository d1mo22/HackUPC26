const express = require('express');
const multer = require('multer');
const cors = require('cors');
const { exec } = require('child_process');
const fs = require('fs');
const path = require('path');

const app = express();
const PORT = 3001;

app.use(cors());
app.use(express.json());

// Configuració de multer per guardar els CSVs pujats
const storage = multer.diskStorage({
  destination: (req, file, cb) => {
    const uploadDir = path.join(__dirname, 'uploads');
    if (!fs.existsSync(uploadDir)) fs.mkdirSync(uploadDir, { recursive: true });
    cb(null, uploadDir);
  },
  filename: (req, file, cb) => {
    // Preservem el nom original però afegim timestamp per evitar col·lisions
    const unique = Date.now() + '-' + Math.round(Math.random() * 1e9);
    cb(null, unique + '-' + file.originalname);
  }
});

const upload = multer({
  storage,
  fileFilter: (req, file, cb) => {
    if (!file.originalname.match(/\.csv$/i)) {
      return cb(new Error('Només s\'accepten fitxers CSV'), false);
    }
    cb(null, true);
  },
  limits: { fileSize: 50 * 1024 * 1024 } // 50MB per fitxer
});

/**
 * POST /process-csv
 * Rep 4 fitxers CSV (camps: csv1, csv2, csv3, csv4)
 * Els passa al binari C++ compilat
 * Retorna el CSV resultant
 */
app.post('/process-csv', upload.fields([
  { name: 'warehouse', maxCount: 1 },
  { name: 'obstacle', maxCount: 1 },
  { name: 'ceiling', maxCount: 1 },
  { name: 'types_of_bays', maxCount: 1 }
]), async (req, res) => {
  const uploadedFiles = [];

  try {
    // Validem que tenim els 4 CSVs
    const fields = ['warehouse', 'obstacle', 'ceiling', 'types_of_bays'];
    for (const field of fields) {
      if (!req.files?.[field]?.[0]) {
        return res.status(400).json({ error: `Falta el fitxer: ${field}` });
      }
    }

    // Recollim les rutes dels fitxers pujats
    const inputPaths = fields.map(f => req.files[f][0].path);
    uploadedFiles.push(...inputPaths);

    // Ruta del fitxer de sortida
    const outputPath = path.join(__dirname, 'uploads', `output-${Date.now()}.csv`);
    uploadedFiles.push(outputPath);

    // Ruta del binari C++ compilat
    const binaryPath = path.join(__dirname, '..', 'cpp', 'processor');

    // Construïm la comanda: ./processor input1.csv input2.csv input3.csv input4.csv output.csv
    const command = `"${binaryPath}" "${inputPaths[0]}" "${inputPaths[1]}" "${inputPaths[2]}" "${inputPaths[3]}" "${outputPath}"`;

    console.log(`Executant: ${command}`);

    // Executem el binari C++
    await new Promise((resolve, reject) => {
      exec(command, { timeout: 60000 }, (error, stdout, stderr) => {
        if (error) {
          console.error('Error C++:', stderr);
          reject(new Error(`Error processant CSVs: ${stderr || error.message}`));
        } else {
          console.log('C++ stdout:', stdout);
          resolve();
        }
      });
    });

    // Verifiquem que el fitxer de sortida existeix
    if (!fs.existsSync(outputPath)) {
      throw new Error('El processador C++ no ha generat el fitxer de sortida');
    }

    // Llegim i enviem el CSV resultant
    const outputContent = fs.readFileSync(outputPath, 'utf8');

    res.setHeader('Content-Type', 'text/csv');
    res.setHeader('Content-Disposition', 'attachment; filename="resultat.csv"');
    res.send(outputContent);

  } catch (err) {
    console.error('Error:', err.message);
    res.status(500).json({ error: err.message });
  } finally {
    // Netegem els fitxers temporals
    for (const filePath of uploadedFiles) {
      try {
        if (fs.existsSync(filePath)) fs.unlinkSync(filePath);
      } catch (_) {}
    }
  }
});

// Health check
app.get('/health', (req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

app.listen(PORT, () => {
  console.log(`🚀 Servidor Express escoltant al port ${PORT}`);
});