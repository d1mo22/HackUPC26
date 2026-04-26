#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <future>

#include "src/solver_core.h"
#include "src/case_io.h"
#include "src/csv_io.h"
#include "src/scoring.h"

using namespace std;
using Clock = chrono::steady_clock;

int main(int argc, char* argv[]) {
    auto total_start = Clock::now();

    // --parity mode: RESTARTS=1, single-threaded, seed 42. Used for regression
    // testing. Prints "<case>\t<Q>\t<bays>" per case then exits.
    bool parity_mode = (argc >= 2 && string(argv[1]) == "--parity");

    if (parity_mode) {
        vector<string> cases_to_run = discover_test_cases();
        for (auto& c : cases_to_run) {
            auto warehouse = read_warehouse(c + "/warehouse.csv");
            auto obstacles = read_obstacles(c + "/obstacles.csv");
            auto ceiling   = read_ceiling(c + "/ceiling.csv");
            auto types     = read_bays(c + "/types_of_bays.csv");
            double wh_area = available_warehouse_area(warehouse, obstacles);
            bool axis_aligned = warehouse_is_axis_aligned(warehouse);
            auto deadline = Clock::now() + chrono::milliseconds((long long)(CASE_BUDGET_SECONDS * 1000.0));
            rng.seed(42);
            int mode = 0;
            OperatorStats stats;
            auto sol = build_initial(types, warehouse, obstacles, ceiling, wh_area, mode, axis_aligned, 0);
            sol = hill(sol, types, warehouse, obstacles, ceiling, wh_area, mode, stats, axis_aligned, deadline);
            double q = quality(sol, wh_area);
            cout << c << "\t" << fixed << setprecision(6) << q << "\t" << sol.size() << "\n";
        }
        return 0;
    }

    if (argc >= 2) {
        // CLI mode: solve a single case directory passed as argv[1].
        // The directory is expected to contain warehouse.csv, obstacles.csv,
        // ceiling.csv and types_of_bays.csv flat (no CaseX/ subfolder).
        // solution.csv is written into the same directory.
        solve_case(argv[1]);
    } else {
        // Discovery mode: scan CWD for CaseX/ subfolders containing the four CSVs.
        vector<string> cases_to_run = discover_test_cases();

        for (auto& c : cases_to_run) {
            solve_case(c);
        }

        cout << "total elapsed=" << seconds_since(total_start) << "s\n";
    }

    return 0;
}
