// solver.h
#pragma once
#include <string>
#include <vector>
#include <random>
#include "types.h"
#include "validator.h"
#include "scorer.h"
#include "operators.h"

struct OperatorStats {
    long long tried[6]    = {};
    long long improved[6] = {};
};

class Solver {
public:
    explicit Solver(const std::string& case_dir);
    void solve();

private:
    // datos del caso
    std::string                              case_dir_;
    std::vector<Point>                       warehouse_;
    std::vector<Obstacle>                    obstacles_;
    std::vector<std::pair<double,double>>    ceiling_;
    std::vector<BayType>                     types_;
    double                                   wh_area_;

    // construcción inicial
    std::vector<PlacedBay> build_initial(int mode, Operators& ops) const;

    // hill climbing
    std::vector<PlacedBay> hill_climb(std::vector<PlacedBay> sol,
                                      int mode,
                                      Operators& ops,
                                      OperatorStats& stats) const;

    // un restart completo (usado en paralelo)
    std::pair<std::vector<PlacedBay>, OperatorStats> run_restart(int r) const;

    // salida
    void write_solution(const std::vector<PlacedBay>& sol) const;
    void print_stats   (const OperatorStats& stats)        const;

    static constexpr int ITERATIONS   = 450;
    static constexpr int INITIAL_ADDS = 80;
    static constexpr int RESTARTS     = 10;

    static const std::vector<std::string> OP_NAMES;
};