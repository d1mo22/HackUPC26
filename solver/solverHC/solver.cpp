// solver.cpp
#include "solver.h"
#include "warehouse_io.h"
#include "geometry.h"
#include <iostream>
#include <fstream>
#include <future>
#include <algorithm>
#include <cmath>
#include <chrono>
using namespace std;
using Clock = chrono::steady_clock;

const vector<string> Solver::OP_NAMES = {
    "add_bay",
    "replace_bay",
    "fill_aggressive",
    "remove_k_and_refill",
    "shared_gap_refill",
    "upgrade_bay"
};

// ── helpers locales ───────────────────────────────────────────────────────────
namespace {

double seconds_since(Clock::time_point start) {
    return chrono::duration<double>(Clock::now() - start).count();
}

} // namespace anónimo

// ── constructor ───────────────────────────────────────────────────────────────
Solver::Solver(const string& case_dir)
    : case_dir_(case_dir)
{
    warehouse_ = WarehouseIO::read_warehouse(case_dir_ + "/warehouse.csv");
    obstacles_ = WarehouseIO::read_obstacles(case_dir_ + "/obstacles.csv");
    ceiling_   = WarehouseIO::read_ceiling  (case_dir_ + "/ceiling.csv");
    types_     = WarehouseIO::read_bays     (case_dir_ + "/types_of_bays.csv");
    wh_area_   = Geometry::polygon_area(warehouse_);
}

// ── privados ──────────────────────────────────────────────────────────────────
vector<PlacedBay> Solver::build_initial(int mode, Operators& ops) const {
    vector<PlacedBay> sol;
    for (int i = 0; i < INITIAL_ADDS; i++)
        if (!ops.add_bay(sol, mode)) break;
    return sol;
}

vector<PlacedBay> Solver::hill_climb(vector<PlacedBay> sol,
                                      int mode,
                                      Operators& ops,
                                      OperatorStats& stats) const {
    vector<PlacedBay> best   = sol;
    Scorer            scorer(wh_area_);
    double            best_q = scorer.quality(best);

    // operadores activos (índices según OP_NAMES)
    const vector<int> active_ops = {3, 4, 5};

    // rng local para elegir operador (no afecta al rng de ops)
    mt19937 local_rng(42);

    for (int it = 0; it < ITERATIONS; it++) {
        vector<PlacedBay> candidate = best;
        int op = active_ops[local_rng() % active_ops.size()];
        stats.tried[op]++;

        if      (op == 0) ops.add_bay          (candidate, mode);
        else if (op == 1) ops.replace_bay      (candidate, mode);
        else if (op == 2) ops.fill_aggressive  (candidate, mode);
        else if (op == 3) ops.remove_k_refill  (candidate, mode);
        else if (op == 4) ops.shared_gap_refill(candidate, mode);
        else              ops.upgrade_bay      (candidate, mode);

        double q = scorer.quality(candidate);
        if (q < best_q) {
            best   = candidate;
            best_q = q;
            stats.improved[op]++;
        }
    }
    return best;
}

pair<vector<PlacedBay>, OperatorStats> Solver::run_restart(int r) const {
    int     mode = r % 4;
    mt19937 rng(42 + r * 1000 + (int)case_dir_[4]);

    Validator validator(warehouse_, obstacles_, ceiling_);
    Scorer    scorer   (wh_area_);
    Operators ops      (warehouse_, obstacles_, ceiling_, types_,
                        validator, scorer, rng);

    cout << "Restart " << r + 1 << "/" << RESTARTS
         << " started | mode=" << mode << "\n";

    auto sol = build_initial(mode, ops);

    OperatorStats stats;
    sol = hill_climb(sol, mode, ops, stats);

    Scorer detail_scorer(wh_area_);
    auto [af, lf, pf, qf] = detail_scorer.details(sol);
    bool valid = validator.is_valid_solution(sol);

    cout << "Restart " << r + 1
         << " -> bays="  << sol.size()
         << " area="     << af
         << " loads="    << lf
         << " price="    << pf
         << " Q="        << qf
         << " valid="    << valid << "\n";

    return {sol, stats};
}

void Solver::write_solution(const vector<PlacedBay>& sol) const {
    string   out_path = case_dir_ + "/solution.csv";
    ofstream out(out_path);
    out << "Id,X,Y,Rotation\n";
    for (auto& p : sol)
        out << p.id << ","
            << llround(p.x) << ","
            << llround(p.y) << ","
            << p.angle << "\n";
    cout << "Written: " << out_path << "\n";
}

void Solver::print_stats(const OperatorStats& stats) const {
    cout << "\nOperator stats for " << case_dir_ << ":\n";
    for (int i = 0; i < 6; i++) {
        double rate = stats.tried[i] > 0
                      ? 100.0 * stats.improved[i] / stats.tried[i]
                      : 0.0;
        cout << OP_NAMES[i]
             << " | tried="    << stats.tried[i]
             << " | improved=" << stats.improved[i]
             << " | rate="     << rate << "%\n";
    }
}

// ── público ───────────────────────────────────────────────────────────────────
void Solver::solve() {
    auto case_start = Clock::now();
    cout << "\n=== Solving " << case_dir_ << " ===\n";

    // lanzar restarts en paralelo
    vector<future<pair<vector<PlacedBay>, OperatorStats>>> futures;
    for (int r = 0; r < RESTARTS; r++)
        futures.push_back(async(launch::async, &Solver::run_restart, this, r));

    // recoger resultados
    vector<PlacedBay> best_global;
    double            best_global_q = 1e100;
    OperatorStats     total_stats;
    Validator         final_validator(warehouse_, obstacles_, ceiling_);
    Scorer            final_scorer   (wh_area_);

    for (int r = 0; r < RESTARTS; r++) {
        auto [sol, stats] = futures[r].get();

        for (int i = 0; i < 6; i++) {
            total_stats.tried[i]    += stats.tried[i];
            total_stats.improved[i] += stats.improved[i];
        }

        auto [a, l, pr, q] = final_scorer.details(sol);
        bool valid = final_validator.is_valid_solution(sol);
        if (valid && q < best_global_q) {
            best_global   = sol;
            best_global_q = q;
        }
    }

    // validación final
    if (!final_validator.is_valid_solution(best_global))
        best_global.clear();

    write_solution(best_global);

    auto [a, l, pr, q] = final_scorer.details(best_global);
    cout << "BEST " << case_dir_ << "\n"
         << "bays="  << best_global.size()
         << " area=" << a
         << " loads=" << l
         << " price=" << pr
         << " Q="     << q << "\n";

    print_stats(total_stats);
    cout << "[time] " << case_dir_
         << " elapsed=" << seconds_since(case_start) << "s\n";
}