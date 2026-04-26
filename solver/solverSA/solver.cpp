// solver.cpp
#include "solver.h"
#include "warehouse_io.h"
#include "geometry.h"
#include <iostream>
#include <fstream>
#include <future>
#include <algorithm>
#include <array>
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
vector<PlacedBay> Solver::build_initial(int mode, Operators& ops,
                                        mt19937& rng) const {
    {
        lock_guard<mutex> lk(elite_mutex_);
        if (!elite_pool_.empty() && rng() % 3 != 0) {
            auto sol = elite_pool_[0];
            int to_remove = max(1, (int)sol.size() / 6);
            for (int i = 0; i < to_remove && !sol.empty(); i++) {
                int idx = rng() % sol.size();
                sol.erase(sol.begin() + idx);
            }
            for (int i = 0; i < INITIAL_ADDS; i++)
                if (!ops.add_bay(sol, mode)) break;
            return sol;
        }
    }

    vector<PlacedBay> sol;
    for (int i = 0; i < INITIAL_ADDS; i++)
        if (!ops.add_bay(sol, mode)) break;
    return sol;
}

vector<PlacedBay> Solver::simulated_annealing(vector<PlacedBay> sol,
                                               int mode,
                                               Operators& ops,
                                               OperatorStats& stats,
                                               mt19937& rng) const {
    Scorer scorer(wh_area_);

    vector<PlacedBay> current   = sol;
    vector<PlacedBay> best      = sol;
    double            current_q = scorer.quality(current);
    double            best_q    = current_q;

    // Operadores ponderados: exploración fuerte y mutaciones locales.
    array<double, 6> op_weight = {1, 1, 2, 3, 3, 2};  // pesos iniciales
    array<int, 6>    op_success = {};
    array<int, 6>    op_tried_local = {};
    const int ADAPT_WINDOW = 200; 

    uniform_real_distribution<double> unit01(0.0, 1.0);
    int iters_no_improve = 0;
    const int STAGNATION_LIMIT = ITERATIONS / 10;  // 10% de iters sin mejora = reheat
    const double REHEAT_FACTOR  = 3.0;             // multiplica T al reactivar
    const double initial_temp = max(1.0, 0.08 * current_q);
    const double final_temp   = max(1.0, 0.005 * initial_temp);
    const double cooling      = pow(final_temp / initial_temp,
                                    1.0 / max(1, ITERATIONS - 1));

    double temperature = initial_temp;

    auto select_op = [&]() -> int {
        double total = 0.0;
        for (double w : op_weight) total += w;
        double r = unit01(rng) * total;
        double acc = 0.0;
        for (int i = 0; i < 6; i++) {
            acc += op_weight[i];
            if (r <= acc) return i;
        }
        return 5;
    };

    for (int it = 0; it < ITERATIONS; it++) {
        if (it > 0 && it % ADAPT_WINDOW == 0) {
            for (int i = 0; i < 6; i++) {
                double rate = op_tried_local[i] > 0
                              ? (double)op_success[i] / op_tried_local[i]
                              : 0.1;
                op_weight[i] = max(0.5, op_weight[i] * 0.7 + rate * 10.0 * 0.3);
                op_success[i] = op_tried_local[i] = 0;
            }
        }

        vector<PlacedBay> candidate = current;
        int op = select_op();
        stats.tried[op]++;
        op_tried_local[op]++;

        if      (op == 0) ops.add_bay          (candidate, mode);
        else if (op == 1) ops.replace_bay      (candidate, mode);
        else if (op == 2) ops.fill_aggressive  (candidate, mode);
        else if (op == 3) ops.remove_k_refill  (candidate, mode);
        else if (op == 4) ops.shared_gap_refill(candidate, mode);
        else              ops.upgrade_bay      (candidate, mode);

        double cand_q = scorer.quality(candidate);
        double delta  = cand_q - current_q;

        if (cand_q < current_q) {
            stats.improved[op]++;
            op_success[op]++;
        }

        bool accept = false;
        if (delta <= 0.0) {
            accept = true;
        } else {
            double p_accept = exp(-delta / max(temperature, 1e-9));
            accept = unit01(rng) < p_accept;
        }

        if (accept) {
            current   = move(candidate);
            current_q = cand_q;
        }

        if (current_q < best_q) {
            best = current;
            best_q = current_q;
            iters_no_improve = 0;   // reset contador
        } else {
            iters_no_improve++;
        }

        if (iters_no_improve >= STAGNATION_LIMIT) {
            temperature = min(initial_temp, temperature * REHEAT_FACTOR);
            iters_no_improve = 0;
            // Kick: volver a la mejor solución conocida con probabilidad 0.5
            if (unit01(rng) < 0.5) current = best, current_q = best_q;
        }
        temperature *= cooling;
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

    auto sol = build_initial(mode, ops, rng);

    OperatorStats stats;
    sol = simulated_annealing(sol, mode, ops, stats, rng);

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

    {
        lock_guard<mutex> lk(elite_mutex_);
        Scorer pool_scorer(wh_area_);
        elite_pool_.push_back(sol);
        sort(elite_pool_.begin(), elite_pool_.end(), [&](const auto& a, const auto& b) {
            return pool_scorer.quality(a) < pool_scorer.quality(b);
        });
        if ((int)elite_pool_.size() > ELITE_SIZE)
            elite_pool_.resize(ELITE_SIZE);
    }

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