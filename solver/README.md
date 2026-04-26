# HackUPC26

## solver_heuristic.cpp — deterministic constructive solver

Fast (~1.2s total for all cases), fully deterministic, no RNG. Runs 12 variants in parallel and picks the one with lowest Q.

### Build

```bash
# macOS with Homebrew libomp
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic solver_heuristic.cpp

# Without OpenMP (single-threaded)
g++ -O3 -std=c++17 -o /tmp/solver_heuristic solver_heuristic.cpp
```

### Run

```bash
cd solver && /tmp/solver_heuristic
```

Writes `CaseN/solution.csv` for each case found in the working directory.