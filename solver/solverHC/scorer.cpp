// scorer.cpp
#include "scorer.h"
#include <cmath>
#include <algorithm>
using namespace std;

Scorer::Scorer(double warehouse_area) : wh_area_(warehouse_area) {}

double Scorer::quality(const vector<PlacedBay>& sol) const {
    if (sol.empty()) return 1e100;
    double total_price = 0;
    double total_loads = 0;
    double area = 0;
    for (auto& p : sol) {
        total_price += p.price;
        total_loads += p.loads;
        area += (double)p.w * p.d;
    }
    double base     = total_price / max(1.0, total_loads);
    double exponent = 2.0 - (area / wh_area_);
    return pow(base, exponent);
}

double Scorer::used_area(const vector<PlacedBay>& sol) const {
    double area = 0;
    for (auto& p : sol)
        area += (double)p.w * p.d;
    return area;
}

tuple<double,int,int,double> Scorer::details(const vector<PlacedBay>& sol) const {
    double area  = 0;
    int    loads = 0;
    int    price = 0;
    for (auto& p : sol) {
        area  += (double)p.w * p.d;
        loads += p.loads;
        price += p.price;
    }
    return {area, loads, price, quality(sol)};
}

double Scorer::bay_score(const BayType& t, int mode) const {
    double area       = (double)t.w * t.d;
    double reserved   = (double)t.w * (t.d + t.gap);
    double price_load = (double)t.price / max(1.0, (double)t.loads);
    double area_ratio = area / max(1.0, wh_area_);    
    double gap_ratio  = (reserved - area) / max(1.0, reserved);

    if (mode == CHEAP_LOAD) return -price_load;
    if (mode == BIG_AREA)   return area_ratio;
    if (mode == LOW_GAP)    return -gap_ratio + 0.2 * area_ratio;
    return 2.0 * area_ratio - 1.0 * price_load - 0.5 * gap_ratio;
}

double Scorer::candidate_score(const BayType& t, double x, double y,
                                int angle, int mode) const {
    double s = bay_score(t, mode); 
    if (angle == 0 || angle == 90 || angle == 180 || angle == 270)
        s += 0.05;
    s -= 0.000001 * (x + y);
    return s;
}