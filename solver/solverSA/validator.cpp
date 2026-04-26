// validator.cpp
#include "validator.h"
#include "geometry.h"
#include <iostream>
#include <algorithm>
using namespace std;

namespace {
vector<Point> bay_poly(const PlacedBay& p) {
    return Geometry::make_rotated_rect(p.x, p.y, p.w, p.d, p.angle);
}

vector<Point> gap_poly(const PlacedBay& p) {
    return Geometry::gap_poly_values(p.x, p.y, p.w, p.d, p.gap, p.angle);
}

vector<Point> obstacle_poly(const Obstacle& o) {
    return {
        {o.x,       o.y      },
        {o.x + o.w, o.y      },
        {o.x + o.w, o.y + o.d},
        {o.x,       o.y + o.d}
    };
}

pair<double,double> minmax_x(const vector<Point>& poly) {
    double mn = poly[0].x, mx = poly[0].x;
    for (auto p : poly) {
        mn = min(mn, p.x);
        mx = max(mx, p.x);
    }
    return {mn, mx};
}

}

Validator::Validator(const vector<Point>&                    warehouse,
                     const vector<Obstacle>&                 obstacles,
                     const vector<pair<double,double>>&      ceiling)
    : warehouse_(warehouse), obstacles_(obstacles), ceiling_(ceiling) {}

double Validator::min_ceiling_between(double x1, double x2) const {
    if (ceiling_.empty()) return 1e18;
    if (x1 > x2) swap(x1, x2);
    double ans = 1e18;
    for (int i = 0; i < (int)ceiling_.size(); i++) {
        double seg_x1 = ceiling_[i].first;
        double h      = ceiling_[i].second;
        double seg_x2 = (i + 1 < (int)ceiling_.size())
                        ? ceiling_[i + 1].first
                        : 1e18;
        double left  = max(x1, seg_x1);
        double right = min(x2, seg_x2);
        if (left < right + EPS)
            ans = min(ans, h);
    }
    return ans;
}

bool Validator::valid_candidate(double x, double y, int w, int d, int h, int gap,
                                 int angle,
                                 const vector<PlacedBay>& sol,
                                 int ignore) const {
    vector<Point> candidate_bay = Geometry::make_rotated_rect(x, y, w, d, angle);
    vector<Point> candidate_gap = Geometry::gap_poly_values(x, y, w, d, gap, angle);

    if (!Geometry::polygon_inside_polygon(candidate_bay, warehouse_)) return false;
    if (!candidate_gap.empty())
        if (!Geometry::polygon_inside_polygon(candidate_gap, warehouse_)) return false;

    for (auto& o : obstacles_) {
        auto op = obstacle_poly(o);
        if (Geometry::polygons_overlap_area(candidate_bay, op)) return false;
        if (!candidate_gap.empty() && Geometry::polygons_overlap_area(candidate_gap, op))
            return false;
    }

    for (int i = 0; i < (int)sol.size(); i++) {
        if (i == ignore) continue;
        vector<Point> other_bay = bay_poly(sol[i]);
        vector<Point> other_gap = gap_poly(sol[i]);
        if (Geometry::polygons_overlap_area(candidate_bay, other_bay)) return false;
        if (!other_gap.empty() && Geometry::polygons_overlap_area(candidate_bay, other_gap))
            return false;
        if (!candidate_gap.empty() && Geometry::polygons_overlap_area(candidate_gap, other_bay))
            return false;
    }

    auto bx     = minmax_x(candidate_bay);
    double min_h = min_ceiling_between(bx.first, bx.second);
    if (h > min_h + EPS) return false;

    return true;
}

bool Validator::is_valid_solution(const vector<PlacedBay>& sol) const {
    for (int i = 0; i < (int)sol.size(); i++) {
        const auto& p = sol[i];
        if (!valid_candidate(p.x, p.y, p.w, p.d, p.h, p.gap,
                             p.angle, sol, i)) {
            cerr << "Invalid bay " << i << "\n";
            return false;
        }
    }
    return true;
}