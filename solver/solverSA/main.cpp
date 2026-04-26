// main.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include "solver.h"
using namespace std;
using Clock = chrono::steady_clock;

int main() {
    auto total_start = Clock::now();
    const vector<string> CASES = {"Case0", "Case1", "Case2", "Case3"};

    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");
        if (f.good()) {
            Solver solver(c);
            solver.solve();
        } else {
            cout << "Skipping " << c << "\n";
        }
    }

    double elapsed = chrono::duration<double>(Clock::now() - total_start).count();
    cout << "\n[time] total elapsed=" << elapsed << "s\n";
    return 0;
}