// operators.cpp
#include "operators.h"
#include "geometry.h"
#include <algorithm>
#include <cmath>
using namespace std;

const vector<int> Operators::ANGLES = {
    0,  10,  20,  30,  40,  50,  60,  70,  80,  90,
    100, 110, 120, 130, 140, 150, 160, 170,
    180, 190, 200, 210, 220, 230, 240, 250, 260, 270,
    280, 290, 300, 310, 320, 330, 340, 350
};

// ── constructor ───────────────────────────────────────────────────────────────
Operators::Operators(const vector<Point>&                    warehouse,
                     const vector<Obstacle>&                 obstacles,
                     const vector<pair<double,double>>&      ceiling,
                     const vector<BayType>&                  types,
                     const Validator&                        validator,
                     const Scorer&                           scorer,
                     mt19937&                                rng)
    : warehouse_(warehouse)
    , obstacles_(obstacles)
    , ceiling_  (ceiling)
    , types_    (types)
    , validator_(validator)
    , scorer_   (scorer)
    , rng_      (rng)
{}

// ── helpers privados ──────────────────────────────────────────────────────────
vector<Point> Operators::candidate_points(const vector<PlacedBay>& sol) const {
    vector<Point> pts;
    for (auto p : warehouse_) pts.push_back(p);
    for (int i = 0; i < (int)warehouse_.size(); i++) {
        Point a = warehouse_[i];
        Point b = warehouse_[(i + 1) % warehouse_.size()];
        pts.push_back({round((a.x + b.x) * 0.5), round((a.y + b.y) * 0.5)});
    }
    for (auto& o : obstacles_) {
        pts.push_back({o.x,       o.y      });
        pts.push_back({o.x + o.w, o.y      });
        pts.push_back({o.x,       o.y + o.d});
        pts.push_back({o.x + o.w, o.y + o.d});
        pts.push_back({round(o.x + 0.5 * o.w), round(o.y + 0.5 * o.d)});
    }
    for (auto& p : sol) {
        auto bp = Geometry::make_rotated_rect(p.x, p.y, p.w, p.d, p.angle);
        auto gp = Geometry::gap_poly_values  (p.x, p.y, p.w, p.d, p.gap, p.angle);
        pts.push_back({round(p.x), round(p.y)});
        pts.push_back({round(p.x + 0.5 * p.w), round(p.y + 0.5 * p.d)});
        for (auto q : bp) pts.push_back({round(q.x), round(q.y)});
        for (auto q : gp) pts.push_back({round(q.x), round(q.y)});
        for (int i = 0; i < (int)bp.size(); i++) {
            Point a = bp[i];
            Point b = bp[(i + 1) % bp.size()];
            pts.push_back({round((a.x + b.x) * 0.5), round((a.y + b.y) * 0.5)});
        }
        for (int i = 0; i < (int)gp.size(); i++) {
            Point a = gp[i];
            Point b = gp[(i + 1) % gp.size()];
            pts.push_back({round((a.x + b.x) * 0.5), round((a.y + b.y) * 0.5)});
        }
    }

    Bounds b = Geometry::polygon_bounds(warehouse_);
    const int gx = 16;
    const int gy = 16;
    if (b.max_x - b.min_x > EPS && b.max_y - b.min_y > EPS) {
        for (int ix = 0; ix <= gx; ix++) {
            double x = b.min_x + (b.max_x - b.min_x) * ix / gx;
            for (int iy = 0; iy <= gy; iy++) {
                double y = b.min_y + (b.max_y - b.min_y) * iy / gy;
                pts.push_back({round(x), round(y)});
            }
        }
    }

    shuffle(pts.begin(), pts.end(), rng_);
    if ((int)pts.size() > 600) pts.resize(600);
    return pts;
}

PlacedBay Operators::make_candidate(const BayType& t, double x, double y, int angle) const {
    return {t.id, x, y, t.w, t.d, t.h, t.gap, angle, t.price, t.loads};
}

// ── operadores públicos ───────────────────────────────────────────────────────
bool Operators::add_bay(vector<PlacedBay>& sol, int mode) const {
    auto pts = candidate_points(sol);

    vector<BayType> sorted_types = types_;
    sort(sorted_types.begin(), sorted_types.end(), [&](const BayType& a, const BayType& b) {
        return scorer_.bay_score(a, mode) > scorer_.bay_score(b, mode);
    });

    PlacedBay best;
    bool   found  = false;
    double best_s = -1e100;
    int    point_limit = min(MAX_POINTS_ADD, (int)pts.size());

    for (int pi = 0; pi < point_limit; pi++) {
        double x = pts[pi].x;
        double y = pts[pi].y;
        for (auto& t : sorted_types) {
            vector<int> angles = ANGLES;
            shuffle(angles.begin(), angles.end(), rng_);
            int limit = min(ANGLE_SAMPLE, (int)angles.size());
            for (int ai = 0; ai < limit; ai++) {
                int angle = angles[ai];
                if (validator_.valid_candidate(x, y, t.w, t.d, t.h, t.gap, angle, sol)) {
                    double s = scorer_.candidate_score(t, x, y, angle, mode, sol);
                    if (s > best_s) {
                        best_s = s;
                        best   = make_candidate(t, x, y, angle);
                        found  = true;
                    }
                }
            }
        }
    }
    if (found) { sol.push_back(best); return true; }
    return false;
}

void Operators::fill_aggressive(vector<PlacedBay>& sol, int mode) const {
    for (int i = 0; i < 5; i++)
        if (!add_bay(sol, mode)) break;
}

void Operators::replace_bay(vector<PlacedBay>& sol, int mode) const {
    if (sol.empty()) return;
    vector<PlacedBay> backup = sol;
    int idx = rng_() % sol.size();
    sol.erase(sol.begin() + idx);
    bool ok = add_bay(sol, mode);
    if (!ok || scorer_.quality(sol) > scorer_.quality(backup))
        sol = backup;
}

void Operators::remove_k_refill(vector<PlacedBay>& sol, int mode) const {
    if (sol.empty()) return;
    vector<PlacedBay> backup = sol;
    int k = min(4, max(1, (int)sol.size() / 5));
    for (int i = 0; i < k && !sol.empty(); i++) {
        int idx = rng_() % sol.size();
        sol.erase(sol.begin() + idx);
    }
    for (int i = 0; i < k + 3; i++)
        add_bay(sol, mode);
    if (scorer_.quality(sol) > scorer_.quality(backup))
        sol = backup;
}

void Operators::shared_gap_refill(vector<PlacedBay>& sol, int mode) const {
    if (sol.empty()) return;
    vector<PlacedBay> backup = sol;
    int k = min(6, max(2, (int)sol.size() / 4));
    for (int i = 0; i < k && !sol.empty(); i++) {
        int idx = rng_() % sol.size();
        sol.erase(sol.begin() + idx);
    }

    const vector<int> shared_angles = {
        0, 180, 90, 270, 10, 190, 80, 260, 100, 280, 170, 350
    };

    auto add_with_angles = [&](const vector<int>& ang_override) {
        auto pts = candidate_points(sol);
        vector<BayType> sorted_types = types_;
        sort(sorted_types.begin(), sorted_types.end(), [&](const BayType& a, const BayType& b){
            return scorer_.bay_score(a, mode) > scorer_.bay_score(b, mode);
        });
        PlacedBay best; bool found = false; double best_s = -1e100;
        int point_limit = min(MAX_POINTS_ADD, (int)pts.size());
        for (int pi = 0; pi < point_limit; pi++) {
            double x = pts[pi].x, y = pts[pi].y;
            for (auto& t : sorted_types) {
                vector<int> angles = ang_override;
                shuffle(angles.begin(), angles.end(), rng_);
                int limit = min(ANGLE_SAMPLE, (int)angles.size());
                for (int ai = 0; ai < limit; ai++) {
                    int angle = angles[ai];
                    if (validator_.valid_candidate(x, y, t.w, t.d, t.h, t.gap, angle, sol)) {
                        double s = scorer_.candidate_score(t, x, y, angle, mode, sol);
                        if (s > best_s) { best_s = s; best = make_candidate(t,x,y,angle); found = true; }
                    }
                }
            }
        }
        if (found) sol.push_back(best);
    };

    for (int i = 0; i < k + 5; i++)
        add_with_angles(shared_angles);

    if (scorer_.quality(sol) > scorer_.quality(backup))
        sol = backup;
}

void Operators::upgrade_bay(vector<PlacedBay>& sol, int mode) const {
    if (sol.empty()) return;
    int idx = rng_() % sol.size();
    PlacedBay old = sol[idx];
    sol.erase(sol.begin() + idx);

    PlacedBay best   = old;
    sol.push_back(old);
    double best_q = scorer_.quality(sol);
    sol.pop_back();
    bool      found  = false;

    for (auto& t : types_) {
        vector<int> angles = ANGLES;
        shuffle(angles.begin(), angles.end(), rng_);
        int limit = min(ANGLE_SAMPLE, (int)angles.size());
        for (int i = 0; i < limit; i++) {
            int angle = angles[i];
            if (validator_.valid_candidate(old.x, old.y, t.w, t.d, t.h, t.gap, angle, sol)) {
                PlacedBay candidate = make_candidate(t, old.x, old.y, angle);
                sol.push_back(candidate);
                double q = scorer_.quality(sol);
                sol.pop_back();
                if (q < best_q) { best_q = q; best = candidate; found = true; }
            }
        }
    }
    sol.push_back(found ? best : old);
}