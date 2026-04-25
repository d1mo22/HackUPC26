// operators.h
#pragma once
#include <vector>
#include <random>
#include "types.h"
#include "validator.h"
#include "scorer.h"

class Operators {
public:
    Operators(const std::vector<Point>&                    warehouse,
              const std::vector<Obstacle>&                 obstacles,
              const std::vector<std::pair<double,double>>& ceiling,
              const std::vector<BayType>&                  types,
              const Validator&                             validator,
              const Scorer&                                scorer,
              std::mt19937&                                rng);

    bool add_bay          (std::vector<PlacedBay>& sol, int mode) const;
    void fill_aggressive  (std::vector<PlacedBay>& sol, int mode) const;
    void replace_bay      (std::vector<PlacedBay>& sol, int mode) const;
    void remove_k_refill  (std::vector<PlacedBay>& sol, int mode) const;
    void shared_gap_refill(std::vector<PlacedBay>& sol, int mode) const;
    void upgrade_bay      (std::vector<PlacedBay>& sol, int mode) const;

private:
    std::vector<Point> candidate_points(const std::vector<PlacedBay>& sol) const;
    PlacedBay          make_candidate  (const BayType& t, double x, double y, int angle) const;

    const std::vector<Point>&                    warehouse_;
    const std::vector<Obstacle>&                 obstacles_;
    const std::vector<std::pair<double,double>>& ceiling_;
    const std::vector<BayType>&                  types_;
    const Validator&                             validator_;
    const Scorer&                                scorer_;
    std::mt19937&                                rng_;

    static constexpr int MAX_POINTS_ADD = 80;
    static constexpr int ANGLE_SAMPLE   = 14;

    static const std::vector<int> ANGLES;
};