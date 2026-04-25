// scorer.h
#pragma once
#include <vector>
#include <tuple>
#include "types.h"

enum InitMode {
    CHEAP_LOAD = 0,
    BIG_AREA   = 1,
    LOW_GAP    = 2,
    BALANCED   = 3
};

class Scorer {
public:
    explicit Scorer(double warehouse_area);

    double quality   (const std::vector<PlacedBay>& sol) const;
    double used_area (const std::vector<PlacedBay>& sol) const;

    std::tuple<double,int,int,double>
           details   (const std::vector<PlacedBay>& sol) const;

    double bay_score      (const BayType& t, int mode) const;
    double candidate_score(const BayType& t, double x, double y,
                           int angle, int mode) const;

private:
    double wh_area_;
};