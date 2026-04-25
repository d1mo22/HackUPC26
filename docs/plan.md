# Warehouse Optimizer — Plan de Implementación

**Reto**: HackUPC 2026 — Mecalux Warehouse Optimizer
**Equipo**: 4 personas
**Duración**: ~36 horas

---

## 0. Resumen ejecutivo

Construimos un sistema que, dado un almacén (polígono rectilíneo con obstáculos y techo variable) y un catálogo de tipos de bays (estanterías), genera una colocación 2D de bays que maximiza la métrica de calidad `Q`:

```
Q = (Σ price/loads)^(2 - area_bays/area_warehouse)
```

Objetivo: **minimizar `price/loads` por bay y maximizar área cubierta**.

**Stack:**
- Solver: **C++20** (binario standalone, CLI, compilado con CMake).
- UI: **React + Canvas 2D + Vite + Tailwind**.
- Integración: la UI lanza el binario del solver localmente.

**Output esperado**: CSV con `Id, X, Y, Rotation` por bay colocada.

---

## 1. Reparto de roles

| Persona | Responsabilidad principal | Área secundaria |
|---------|---------------------------|------------------|
| **P1** (full-stack) | UI + integración solver↔UI | Pitch y demo |
| **P2** (algoritmo + back) | Solver core: parser, representación espacio, heurística constructiva | Optimización de rendimiento |
| **P3** (algoritmo + back) | Mejora local (Simulated Annealing / Hill Climbing) | Tuning de parámetros |
| **P4** (back + front) | Validador, casos de test, scoring script | Soporte a UI cuando solver sea estable |

---

## 2. Preguntas críticas para Mecalux (preguntar día 1)

Estas 3 decisiones cambian el algoritmo, así que hay que resolverlas cuanto antes:

1. **Rotación**: ¿solo múltiplos de 90° (0, 90, 180, 270) o grados arbitrarios? En los ejemplos aparecen valores como 70 y 180 — aclarar si son reales o error del PDF.
2. **Pasillos**: ¿hay que dejar espacio de acceso entre filas de bays para forklifts, o pueden pegarse sin restricción?
3. **Gap**: ¿el campo `gap` es entre niveles (vertical, irrelevante para packing 2D) o entre bays adyacentes (horizontal, sí afecta)?

**Plan B si no hay respuesta rápida**: asumir rotaciones de 90°, sin pasillos obligatorios, gap vertical (irrelevante). Validar con un caso de ejemplo que el output sea aceptado.

---

## 3. Arquitectura general

```
┌─────────────────────────────────────────────────┐
│                     UI (React)                  │
│  ┌─────────────┐  ┌──────────────────────────┐  │
│  │ Load CSVs   │  │ Canvas 2D visualizer     │  │
│  │ Run solver  │  │ (warehouse + bays)       │  │
│  │ Metrics     │  │ Zoom / pan / tooltip     │  │
│  └─────────────┘  └──────────────────────────┘  │
└──────────────────┬──────────────────────────────┘
                   │ (spawn process / HTTP local)
                   ▼
┌─────────────────────────────────────────────────┐
│              Solver binary (Rust)               │
│  parser → space repr → constructive → SA → out  │
└─────────────────────────────────────────────────┘
                   │
                   ▼
              output.csv
```

**Contrato entre partes**: el solver lee 4 CSVs de argumentos y escribe un CSV de salida. La UI parsea esa salida y la pinta. Interfaz simple y fácil de testear por separado.

---

## 4. Backend — Solver (C++20)

**Owner principal: P2. Owner de optimización: P3.**

### 4.1 Estructura del proyecto

```
solver/
├── CMakeLists.txt
├── include/
│   ├── model.hpp        # structs: Warehouse, Bay, Obstacle, Ceiling, Solution
│   ├── parser.hpp       # lectura CSVs
│   ├── space.hpp        # representación del espacio (grid occupancy)
│   ├── constructive.hpp # heurística constructiva (shelf / BLF)
│   ├── annealing.hpp    # simulated annealing
│   ├── scoring.hpp      # cálculo de Q
│   ├── validate.hpp     # validar solución (colisiones, ceiling, polígono)
│   └── output.hpp       # serialización CSV output
├── src/
│   ├── main.cpp         # CLI entry
│   ├── parser.cpp
│   ├── space.cpp
│   ├── constructive.cpp
│   ├── annealing.cpp
│   ├── scoring.cpp
│   ├── validate.cpp
│   └── output.cpp
├── tests/               # tests integrados con casos de ejemplo
│   └── CMakeLists.txt
└── third_party/         # header-only libs (si hace falta)
```

### 4.2 Build y dependencias

- **Build**: CMake (mínimo 3.16), C++20.
- **Compilador**: g++ 11+, clang 13+, MSVC 2022. Cross-platform.
- **Flags release**: `-O3 -march=native -DNDEBUG`. Para debug: `-O0 -g -fsanitize=address,undefined`.
- **Librerías** (todas header-only o STL para evitar problemas de linking):
  - STL para casi todo (`<filesystem>`, `<span>`, `<format>`, `<ranges>`).
  - `<random>` para SA.
  - `<thread>` o `std::async` para multi-start.
  - **CSV**: parsing manual o [fast-cpp-csv-parser](https://github.com/ben-strasser/fast-cpp-csv-parser) (header-only, 1 archivo).
  - **CLI args**: parsing manual o [CLI11](https://github.com/CLIUtils/CLI11) (header-only).
  - `<cassert>` + framework simple para tests (Catch2 header-only si queréis algo más elaborado).

**Regla de oro**: nada que requiera `apt install` o pasos complicados. Los jueces compilan en su máquina, todo en `third_party/` o STL.

### 4.3 Modelo de datos

```cpp
struct Point { int32_t x, y; };

struct Warehouse {
    std::vector<Point> polygon;   // vértices en orden
};

struct Obstacle {
    int32_t x, y, w, d;
};

struct CeilingSegment {
    int32_t x_start;
    int32_t height;
};

struct BayType {
    uint32_t id;
    int32_t  w, d, h;
    int32_t  gap;
    uint32_t loads;
    uint32_t price;
};

struct Placement {
    uint32_t bay_id;
    int32_t  x, y;
    uint16_t rot;   // 0, 90, 180, 270
};

struct Solution {
    std::vector<Placement> placements;
};
```

Usa `std::vector` siempre (no arrays crudos). Las estructuras son triviales (POD) — pásalas por `const&` cuando sean grandes.

### 4.4 Representación del espacio

**Decisión**: grid discreto 2D con resolución adaptativa.

- Resolución por defecto: 100mm (ajustable vía flag).
- Para warehouse 20m × 20m → 200 × 200 = 40k celdas. Trivial.
- Bitmap compacto: `std::vector<uint64_t>` con 64 celdas por palabra. Evita `std::vector<bool>` (rendimiento inconsistente).
- Precómputo: marcar celdas fuera del polígono y celdas de obstáculos como ocupadas.
- Función rápida `ceiling_at(x) -> int32_t` con búsqueda binaria sobre segmentos (`std::upper_bound`).
- Sparse table para `min_ceiling_in_range(x1, x2) -> int32_t` en O(1) consulta.

**Interfaz principal** (ejemplo):

```cpp
class SpaceGrid {
public:
    SpaceGrid(const Warehouse&, const std::vector<Obstacle>&, int32_t resolution);

    bool can_place(const BayType& bay, int32_t x, int32_t y, uint16_t rot) const;
    void place(const BayType& bay, int32_t x, int32_t y, uint16_t rot);
    void unplace(const BayType& bay, int32_t x, int32_t y, uint16_t rot);
    int32_t ceiling_at(int32_t x) const;
    int32_t min_ceiling_range(int32_t x1, int32_t x2) const;

private:
    std::vector<uint64_t> occupied_;
    std::vector<CeilingSegment> ceiling_;
    // sparse table para range min
};
```

### 4.5 Heurística constructiva

**Fase 1 (MVP) — Shelf Packing:**

1. Ordenar tipos de bay por score `nLoads / (price × area)` descendente.
2. Recorrer warehouse en franjas horizontales (shelves) de abajo a arriba.
3. En cada shelf, intentar colocar filas completas del tipo de bay con mejor score que quepa (respetando `ceiling(x)` en todo el rango X).
4. Rellenar huecos con bays más pequeñas o rotadas.

**Fase 2 (si hay tiempo) — Bottom-Left-Fill:**

Para cada bay ordenada, buscar la posición más abajo-izquierda donde cabe. Mejor empaquetado con obstáculos e irregularidades del polígono.

**Rotaciones soportadas** (asumiendo respuesta "90°"): 0 y 90. Si se confirman 180/270, añadir.

### 4.6 Mejora local — Simulated Annealing

**Owner: P3.**

- **Estado**: secuencia ordenada de bays a colocar + rotación de cada una. La constructiva convierte secuencia → layout.
- **Función objetivo**: Q (a maximizar).
- **Vecinos posibles**:
  - Swap de dos bays en la secuencia.
  - Cambio de rotación de una bay.
  - Cambio de tipo (sustituir bay tipo A por tipo B).
  - Eliminar una bay (si mejora Q).
- **Schedule**: enfriamiento geométrico, `T *= 0.995` por iteración.
- **RNG**: `std::mt19937_64` con seed por flag CLI (reproducibilidad).
- **Timeout**: 25s (dejando 5s de margen sobre los 30s permitidos). Usar `std::chrono::steady_clock` para medir.
- **Multi-start con `std::async` / `std::thread`**: lanzar 4-8 runs en paralelo con semillas distintas y quedarse con el mejor resultado. Cada thread trabaja sobre su propia copia del estado.

**Nota de paralelismo**: el `SpaceGrid` NO es thread-safe. Cada thread de SA tiene su propio grid. Compartir solo input (read-only) y un mutex para actualizar el "mejor global".

### 4.7 CLI

```bash
solver --warehouse w.csv --obstacles o.csv --ceiling c.csv --types t.csv --output out.csv [--timeout 25] [--seed 42] [--threads 8]
```

### 4.8 Tests (P4)

- Unit tests para parser (casos bien formados y malformados).
- Unit tests para validador (colisiones, ceiling, fuera de polígono).
- Tests integrados con los casos de ejemplo del enunciado.
- Test de rendimiento: caso sintético grande (50x50m, 500 bays) — debe acabar en <30s.
- Framework: Catch2 (header-only) o asserts manuales con un `main` de tests. Lo que no frene.

### 4.9 Gotchas de C++ a evitar

- **UB con índices firmados/no-firmados**: usa `int32_t` consistentemente para coordenadas, `size_t` solo para índices de vectores.
- **No uses `new`/`delete` manuales**: todo con `std::vector` / `std::unique_ptr`.
- **Activa `-Wall -Wextra -Wpedantic`** desde el primer commit.
- **Compila con sanitizers en modo debug** (`-fsanitize=address,undefined`): detecta corrupciones de memoria que te arruinarían el hackathon a las 3 AM.
- **Overflow**: un grid 500×500 × 4 rotaciones × ... puede pasar de `int32_t` en intermedios. Usa `int64_t` para áreas y sumas totales.
- **CSV con `\r\n` en Windows**: el parser debe tolerar line endings mixtos.

---

## 5. Algoritmo — Detalle de decisiones

**Owners: P2 (constructivo) + P3 (SA).**

### 5.1 Por qué shelf packing como base

- Los ejemplos del enunciado muestran claramente layouts ordenados en filas (slide 3 "or" slide 9).
- Simple de implementar, rápido (<1s para casos normales).
- Fácil de combinar con el ceiling variable: en cada shelf miras el techo en su rango X.

### 5.2 Manejo del ceiling

- Precalcular `min_ceiling_in_range(x1, x2) -> i32` con un sparse table (consulta O(1) tras O(n log n) de precómputo).
- Una bay en X ∈ [x1, x2] solo es válida si `min_ceiling_in_range(x1, x2) >= bay.height`.

### 5.3 Manejo del polígono rectilíneo

- Descomponer el polígono en rectángulos axis-aligned (máximo 2-3 rectángulos en los ejemplos).
- O trivial: marcar en el grid qué celdas están dentro del polígono (ray casting o algoritmo de punto-en-polígono).
- Obstáculos se marcan igual en el grid.

### 5.4 Obstáculos

- Son cajas axis-aligned.
- Se marcan como ocupados en el grid antes de colocar cualquier bay.
- No se pueden rotar (son obstáculos, no piezas).

### 5.5 Score de selección de bay

Al elegir qué tipo colocar primero, combinar dos criterios:

```
score(bay) = (nLoads / price) × rank_by_size
```

- `nLoads / price` favorece bays baratas por carga (mejora la base de Q).
- Favorecer bays grandes al principio (mejora cobertura y baja el exponente).
- En SA, explorar mezclas distintas para no quedarse atrapado.

---

## 6. Frontend — UI (React + Canvas 2D)

**Owner principal: P1. Apoyo: P4 en últimas horas.**

### 6.1 Estructura del proyecto

```
ui/
├── package.json
├── vite.config.ts
├── src/
│   ├── main.tsx
│   ├── App.tsx
│   ├── components/
│   │   ├── FileLoader.tsx       # carga los 4 CSVs
│   │   ├── Canvas.tsx           # render 2D warehouse + bays
│   │   ├── MetricsPanel.tsx     # Q, área, precio, #bays
│   │   ├── BayLegend.tsx        # tipos de bay con color
│   │   └── Controls.tsx         # run solver, zoom, toggle layers
│   ├── hooks/
│   │   ├── useCanvas.ts         # zoom/pan, hit-testing
│   │   └── useSolver.ts         # spawn solver, stream output
│   ├── lib/
│   │   ├── csvParser.ts         # parse CSVs cliente
│   │   ├── geometry.ts          # utilidades geometría
│   │   └── scoring.ts           # recalcula Q en frontend
│   └── types.ts
```

### 6.2 Dependencias

- `react`, `react-dom`, `vite`, `typescript`.
- `tailwindcss` para estilos rápidos.
- `zustand` para estado global (opcional, Context vale).
- `papaparse` para CSVs.
- Nada más — el Canvas es API nativa del navegador.

### 6.3 Funcionalidad mínima (MVP)

1. **Carga de archivos**: drag & drop o selector de los 4 CSVs. Validación básica.
2. **Render warehouse**: dibujar polígono (contorno negro), obstáculos (rectángulos gris rayado).
3. **Botón "Run Solver"**: llama al binario, recibe `output.csv`, parsea, renderiza bays.
4. **Render bays**: rectángulo coloreado por tipo, con ID en el centro. Orientación indicada con flecha o gradiente.
5. **Panel de métricas**: Q, área cubierta (%), precio total, #bays por tipo.
6. **Zoom + pan**: rueda del ratón y drag.

### 6.4 Features extra (si sobra tiempo)

- **Tooltip al pasar ratón** sobre una bay: tipo, dimensiones, altura, precio.
- **Overlay de ceiling**: tinte del fondo según `ceiling(x)` (más oscuro = techo más bajo). Comunica la restricción 3D sin hacer 3D.
- **Animación de colocación**: stream del output del solver y pintar bays según se van colocando.
- **Comparador**: correr solver 2 veces con parámetros distintos y comparar Q lado a lado.
- **Export de imagen**: botón para exportar el canvas como PNG.
- **Modo presentación**: pantalla limpia sin controles, solo visualización.

### 6.5 Detalles de render

- Coordenadas del problema en mm. Convertir a pixels con un factor de escala según zoom.
- Origen (0,0) del problema en la esquina inferior izquierda; invertir eje Y en Canvas.
- Usar `requestAnimationFrame` para el loop de render.
- Hit-testing: mantener un array de bays con sus bounding boxes, iterar en orden inverso al pintado.

### 6.6 Integración con el solver

**Opción A (recomendada para simplicidad)**: servidor local pequeño en Node/Express que:
- Acepta los 4 CSVs vía POST.
- Escribe archivos temporales.
- Spawnea el binario del solver.
- Devuelve el output CSV como respuesta.

**Opción B**: el binario del solver expone un endpoint HTTP simple (p.ej. con [cpp-httplib](https://github.com/yhirose/cpp-httplib), header-only) sirviendo también los archivos estáticos. Un solo binario, pero más trabajo de setup.

Para hackathon, **Opción A**.

---

## 7. Validador + Scoring (P4)

**Crítico: esto debe estar listo en las primeras 3 horas.** Sin validador no sabemos si el solver produce soluciones correctas.

### 7.1 Qué valida

- Todas las bays están dentro del polígono del warehouse.
- Ninguna bay colisiona con obstáculos.
- Ninguna bay colisiona con otra bay.
- Toda bay respeta `bay.height ≤ min_ceiling_in_range(bay.x_range)`.
- IDs del output corresponden a tipos definidos en `types.csv`.
- Rotaciones son válidas (0/90/180/270).

### 7.2 Qué calcula

- Q (métrica oficial).
- Área total ocupada y % sobre área total del warehouse.
- Precio total.
- Cargas totales almacenables.
- #bays por tipo.

### 7.3 Forma del entregable

- Implementado en C++ dentro del solver (`validate.cpp` / `validate.hpp`) para reutilizar en tests.
- Exposición CLI adicional: `solver --validate --output out.csv ...` para que P1 también pueda usarlo desde la UI.

### 7.4 Generador de casos sintéticos

P4 crea un script Python pequeño que genera casos de test:
- Warehouse rectangular configurable.
- N obstáculos aleatorios.
- Techo escalonado con K niveles.
- M tipos de bay con dimensiones y precios aleatorios dentro de rangos razonables.

Sirve para probar el solver con entradas grandes antes del judging.

---

## 8. Timeline (36h)

### H0–H3 — Setup y contratos
- [ ] P1: esqueleto React + Vite + Canvas vacío que pinta un rectángulo.
- [ ] P2: proyecto C++ con CMake, parser de los 4 CSVs, structs del modelo, lectura ejemplo.
- [ ] P3: esqueleto del validador en C++ + cálculo de Q.
- [ ] P4: 3-5 casos de test manuales + generador sintético.
- [ ] Todos: preguntar a Mecalux las 3 dudas críticas.

### H3–H10 — MVP end-to-end
- [ ] P2: heurística constructiva shelf packing v1, sin rotación.
- [ ] P3: validador completo funcional.
- [ ] P1: UI carga los CSVs, pinta warehouse y obstáculos, llama al solver, pinta bays.
- [ ] P4: tests integrados en CI local; probar con todos los casos sintéticos.
- [ ] **Milestone**: tenemos una solución válida aunque no óptima, visualizada.

### H10–H20 — Iteración del solver
- [ ] P2: rotación 0/90 (y 180/270 si aplica), respeto de ceiling preciso, selección por score.
- [ ] P3: implementar simulated annealing sobre la secuencia, multi-start con `rayon`.
- [ ] P1: zoom, pan, tooltip, panel de métricas completo, leyenda de tipos.
- [ ] P4: casos de test de tamaño "judging" (bigger than examples).

### H20–H30 — Pulido y mejora de calidad
- [ ] P3: tuning de SA (temperatura, cooling rate, vecinos).
- [ ] P2: BLF puro si shelf no rinde bien en casos con obstáculos.
- [ ] P1: overlay de ceiling, animación opcional, export PNG.
- [ ] P4: stress testing, benchmarks, detección de regresiones.

### H30–H36 — Presentación
- [ ] P1: preparar demo en vivo con 2-3 casos preparados.
- [ ] Todos: ensayar pitch de 3-5 minutos.
- [ ] Grabar video backup por si el proyector falla.
- [ ] Deploy final del binario del solver (release build con `-O3 -march=native`, sin sanitizers).
- [ ] Dormir algo.

---

## 9. Criterios de éxito

### 9.1 Mínimo viable (para no ir en blanco)
- Solver produce output válido en todos los casos de ejemplo.
- UI muestra la solución con warehouse, obstáculos y bays.
- Métrica Q calculada y mostrada.

### 9.2 Objetivo realista
- Solver con shelf packing + SA básico.
- Multi-start en paralelo.
- UI con zoom, pan, tooltip, métricas en vivo.
- Buenos Q en casos variados.

### 9.3 Stretch goals
- Animación de colocación en vivo.
- Overlay de ceiling visible en UI.
- Comparador de múltiples ejecuciones.
- Export de soluciones como imagen + CSV.
- Benchmark documentado contra los ejemplos del PDF.

---

## 10. Riesgos y mitigaciones

| Riesgo | Mitigación |
|--------|------------|
| Solver tarda >30s en casos grandes | Timeout interno a 25s + devolver mejor solución hasta ahora (anytime) |
| Rotaciones arbitrarias requeridas | Limitar a 90° de todas formas; argumentar que los ejemplos parecen errores |
| Bug de colisión no detectado | Validador obligatorio en cada output, tests exhaustivos desde día 1 |
| UI se come tiempo del solver | Mantener UI simple y funcional; 3D solo si queda tiempo genuino |
| Un miembro del equipo bloqueado | Pair programming en el primer bloqueo, rotar si persiste >1h |
| Solver produce Q peor que greedy trivial | Mantener greedy simple como fallback; SA nunca empeora |
| Segfault / memory bug en C++ | Compilar con `-fsanitize=address,undefined` en debug desde H0; tests con casos pequeños |
| Build no compila en la máquina de jueces | CMake puro, sin deps externas; probar compilación en Linux y macOS durante el hackathon |

---

## 11. Comunicación del equipo

- **Repo Git compartido desde H0**. Branches por persona, PRs rápidos.
- **Standup ligero cada 4h**: qué hice, qué hago, bloqueos.
- **Canal de chat** para dudas rápidas (Discord/Slack/Telegram).
- **Compartir casos de test y outputs en un directorio común** del repo.
- **Dormir por turnos** si váis toda la noche; nadie es productivo con 0h de sueño.

---

## 12. Anexo: fórmula de Q y cómo maximizarla

```
Q = (Σ_bay price_bay / loads_bay) ^ (2 - area_usada / area_warehouse)
```

- **Base** `Σ price/loads`: suma de ratios precio/cargas. **Más bajo es mejor** si el exponente es positivo.
- **Exponente** `2 - ratio_área`: cuando `ratio_área → 1`, exponente → 1; cuando `ratio_área → 0`, exponente → 2.

**Estrategia óptima**:
1. **Cubrir mucha área** → baja el exponente → reduce el efecto amplificador de un precio alto.
2. **Preferir bays con `nLoads/price` alto** → reduce la base.
3. **Evitar bays pequeñas y caras por carga** → son veneno doble (poca área + mala ratio).

El score `nLoads / (price × area)` captura las 3 dimensiones a la vez y es nuestro ranking principal.

---

**Buena suerte. Manos a la obra.**
