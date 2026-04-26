#include "scoring.h"
#include <cmath>

double quality(const vector<PlacedBay>& sol, double warehouse_area) {
    if (sol.empty()) return 1e100;

    double total_price = 0;
    double total_loads = 0;
    double area = 0;

    for (auto& p : sol) {
        total_price += p.price;
        total_loads += p.loads;
        area += (double)p.w * p.d;
    }

    double base = total_price / max(1.0, total_loads);
    double exponent = 2.0 - (area / warehouse_area);

    return pow(base, exponent);
}

QSums compute_sums(const vector<PlacedBay>& sol) {
    QSums s;
    for (auto& p : sol) {
        s.price += p.price;
        s.loads += p.loads;
        s.area += (double)p.w * p.d;
    }
    return s;
}

double quality_from_sums(const QSums& s, double warehouse_area, bool empty) {
    if (empty) return 1e100;
    double base = s.price / max(1.0, s.loads);
    double exponent = 2.0 - (s.area / warehouse_area);
    return pow(base, exponent);
}

double quality_with_added(const QSums& cur, double price, double loads, double area, double warehouse_area) {
    double new_price = cur.price + price;
    double new_loads = cur.loads + loads;
    double new_area = cur.area + area;
    double base = new_price / max(1.0, new_loads);
    double exponent = 2.0 - (new_area / warehouse_area);
    return pow(base, exponent);
}

double used_area(const vector<PlacedBay>& sol) {
    double area = 0;

    for (auto& p : sol) {
        area += (double)p.w * p.d;
    }

    return area;
}

double obstacles_area(const vector<Obstacle>& obstacles) {
    double area = 0;

    for (auto& o : obstacles) {
        area += o.w * o.d;
    }

    return area;
}

double available_warehouse_area(
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles
) {
    return max(1.0, polygon_area(warehouse) - obstacles_area(obstacles));
}

tuple<double, int, int, double> details(const vector<PlacedBay>& sol, double wh_area) {
    double area = 0;
    int loads = 0;
    int price = 0;

    for (auto& p : sol) {
        area += (double)p.w * p.d;
        loads += p.loads;
        price += p.price;
    }

    return {area, loads, price, quality(sol, wh_area)};
}

double bay_score_mode(const BayType& t, int mode, double wh_area) {
    double area = (double)t.w * t.d;
    double reserved = (double)t.w * (t.d + t.gap);
    double price_load = (double)t.price / max(1.0, (double)t.loads);
    double area_ratio = area / max(1.0, wh_area);
    double gap_ratio = (reserved - area) / max(1.0, reserved);

    if (mode == CHEAP_LOAD) {
        return -price_load;
    }

    if (mode == BIG_AREA) {
        return area_ratio;
    }

    if (mode == LOW_GAP) {
        return -gap_ratio + 0.2 * area_ratio;
    }

    return 2.0 * area_ratio - 1.0 * price_load - 0.5 * gap_ratio;
}

double candidate_score_mode(
    const BayType& t,
    double x,
    double y,
    int angle,
    double wh_area,
    int mode
) {
    double s = bay_score_mode(t, mode, wh_area);

    if (angle == 0 || angle == 90 || angle == 180 || angle == 270) {
        s += 0.05;
    }

    s -= 0.000001 * (x + y);

    return s;
}
