// validator.h
#pragma once
#include <vector>
#include <utility>
#include "types.h"

class Validator {
public:
    Validator(const std::vector<Point>&                    warehouse,
              const std::vector<Obstacle>&                 obstacles,
              const std::vector<std::pair<double,double>>& ceiling);

    bool valid_candidate(double x, double y, int w, int d, int h, int gap,
                         int angle,
                         const std::vector<PlacedBay>& sol,
                         int ignore = -1) const;

    bool is_valid_solution(const std::vector<PlacedBay>& sol) const;

private:
    double min_ceiling_between(double x1, double x2) const;

    const std::vector<Point>&                    warehouse_;
    const std::vector<Obstacle>&                 obstacles_;
    const std::vector<std::pair<double,double>>& ceiling_;
};