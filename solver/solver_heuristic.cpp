// solver/solver_heuristic.cpp
// Deterministic heuristic constructive solver.

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <tuple>
#include <numeric>
#include <string>
#include <array>
#include <chrono>
#include <climits>
#include <functional>
#ifdef _OPENMP
#include <omp.h>
#endif
using namespace std;
using Clock = chrono::steady_clock;

const vector<string> CASES = {
    "Case0", "Case1", "Case2", "Case3",
    "CaseWeird", "Case40",
    "CaseAngledA", "CaseAngledB", "CaseAngledC", "CaseAngledD",
    "CaseDiagArmA", "CaseDiagArmB", "CaseDiagArmC", "CaseDiagArmD",
    "CaseForcedAngle"
};
const double EPS = 1e-7;
const double PI = acos(-1.0);

// ============== IMPROVEMENT CONSTANTS ==============
const double TOTAL_BUDGET_SECONDS = 25.0;
const int    LOOKAHEAD_K          = 10;
const int    INTERIOR_GRID_CAP    = 100;
const double RELAX_FACTOR         = 0.05;
const double W_LOOKAHEAD          = 0.3;
const int    REMOVE_TOP_K_FRAC    = 3;   // try top 1/REMOVE_TOP_K_FRAC bays by removal-ΔQ
const double LOCAL_FILL_RADIUS    = 3.0; // multiplier of max_bay_dim for local fill region

struct Trig { double cos_v, sin_v; };

struct Point { double x, y; };

struct BayType {
    int id, w, d, h, gap, loads, price;
};

struct PlacedBay {
    int id;
    double x, y;
    int w, d, h, gap;
    int angle;
    int price, loads;
    // cached geometry (refreshed via refresh_cache after any field change)
    vector<Point> bay_pts;
    vector<Point> gap_pts;
    double bay_min_x, bay_min_y, bay_max_x, bay_max_y;
    double gap_min_x, gap_min_y, gap_max_x, gap_max_y;
};

struct Obstacle {
    double x, y, w, d;
};

struct Bounds {
    double min_x, max_x, min_y, max_y;
};

// ================= GEOMETRY =================

double cross(Point a, Point b, Point c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

double dotp(Point a, Point b) {
    return a.x * b.x + a.y * b.y;
}

double polygon_area(const vector<Point>& p) {
    double a = 0;

    for (int i = 0; i < (int)p.size(); i++) {
        Point p1 = p[i];
        Point p2 = p[(i + 1) % p.size()];
        a += p1.x * p2.y - p2.x * p1.y;
    }

    return fabs(a) / 2.0;
}

bool point_on_segment(Point p, Point a, Point b) {
    if (fabs(cross(a, b, p)) > EPS) return false;

    return min(a.x, b.x) - EPS <= p.x && p.x <= max(a.x, b.x) + EPS &&
           min(a.y, b.y) - EPS <= p.y && p.y <= max(a.y, b.y) + EPS;
}

int orient(Point a, Point b, Point c) {
    double v = cross(a, b, c);

    if (fabs(v) < EPS) return 0;

    return v > 0 ? 1 : -1;
}

bool proper_segment_intersection(Point a, Point b, Point c, Point d) {
    int o1 = orient(a, b, c);
    int o2 = orient(a, b, d);
    int o3 = orient(c, d, a);
    int o4 = orient(c, d, b);

    return o1 * o2 < 0 && o3 * o4 < 0;
}

bool point_inside_or_on_polygon(Point p, const vector<Point>& poly) {
    for (int i = 0; i < (int)poly.size(); i++) {
        if (point_on_segment(p, poly[i], poly[(i + 1) % poly.size()])) {
            return true;
        }
    }

    bool inside = false;

    for (int i = 0, j = (int)poly.size() - 1; i < (int)poly.size(); j = i++) {
        Point a = poly[i];
        Point b = poly[j];

        bool crosses_y = (a.y > p.y) != (b.y > p.y);

        if (crosses_y) {
            double x_intersect = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;

            if (p.x < x_intersect) {
                inside = !inside;
            }
        }
    }

    return inside;
}

bool any_proper_segment_crossing(const vector<Point>& a, const vector<Point>& b) {
    for (int i = 0; i < (int)a.size(); i++) {
        Point a1 = a[i];
        Point a2 = a[(i + 1) % a.size()];

        for (int j = 0; j < (int)b.size(); j++) {
            Point b1 = b[j];
            Point b2 = b[(j + 1) % b.size()];

            if (proper_segment_intersection(a1, a2, b1, b2)) {
                return true;
            }
        }
    }

    return false;
}

bool polygon_inside_polygon(const vector<Point>& small, const vector<Point>& big) {
    for (auto p : small) {
        if (!point_inside_or_on_polygon(p, big)) {
            return false;
        }
    }

    if (any_proper_segment_crossing(small, big)) {
        return false;
    }

    return true;
}

void project_poly(const vector<Point>& poly, Point axis, double& mn, double& mx) {
    mn = mx = dotp(poly[0], axis);

    for (int i = 1; i < (int)poly.size(); i++) {
        Point p = poly[i];
        double v = dotp(p, axis);
        mn = min(mn, v);
        mx = max(mx, v);
    }
}

Bounds polygon_bounds(const vector<Point>& poly) {
    Bounds b = {poly[0].x, poly[0].x, poly[0].y, poly[0].y};

    for (auto p : poly) {
        b.min_x = min(b.min_x, p.x);
        b.max_x = max(b.max_x, p.x);
        b.min_y = min(b.min_y, p.y);
        b.max_y = max(b.max_y, p.y);
    }

    return b;
}

bool bounds_disjoint(const Bounds& a, const Bounds& b) {
    return a.max_x <= b.min_x + EPS || b.max_x <= a.min_x + EPS ||
           a.max_y <= b.min_y + EPS || b.max_y <= a.min_y + EPS;
}

bool sat_overlap_positive_area(const vector<Point>& a, const vector<Point>& b) {
    if (bounds_disjoint(polygon_bounds(a), polygon_bounds(b))) {
        return false;
    }

    auto separated_on_axes = [&](const vector<Point>& poly) {
        for (int i = 0; i < (int)poly.size(); i++) {
            Point p1 = poly[i];
            Point p2 = poly[(i + 1) % poly.size()];

            Point edge = {p2.x - p1.x, p2.y - p1.y};
            Point axis = {-edge.y, edge.x};

            double len = hypot(axis.x, axis.y);
            if (len < EPS) continue;

            axis.x /= len;
            axis.y /= len;

            double minA, maxA, minB, maxB;

            project_poly(a, axis, minA, maxA);
            project_poly(b, axis, minB, maxB);

            if (maxA <= minB + EPS || maxB <= minA + EPS) {
                return true;
            }
        }

        return false;
    };

    if (separated_on_axes(a)) return false;
    if (separated_on_axes(b)) return false;

    return true;
}

bool polygons_overlap_area(const vector<Point>& a, const vector<Point>& b) {
    return sat_overlap_positive_area(a, b);
}

const Trig& trig_for_angle(int angle) {
    static const array<Trig, 360> table = [] {
        array<Trig, 360> values{};

        for (int i = 0; i < 360; i++) {
            double rad = i * PI / 180.0;
            values[i] = {cos(rad), sin(rad)};
        }

        return values;
    }();

    int idx = angle % 360;
    if (idx < 0) idx += 360;

    return table[idx];
}

vector<Point> make_rotated_rect(double x, double y, double w, double d, int angle) {
    const Trig& trig = trig_for_angle(angle);
    double ca = trig.cos_v;
    double sa = trig.sin_v;

    array<Point, 4> local = {{
        {0, 0},
        {w, 0},
        {w, d},
        {0, d}
    }};

    vector<Point> res;
    res.reserve(4);

    for (auto p : local) {
        double rx = p.x * ca - p.y * sa;
        double ry = p.x * sa + p.y * ca;

        res.push_back({x + rx, y + ry});
    }

    return res;
}

vector<Point> gap_poly_values(double x, double y, int w, int d, int gap, int angle) {
    if (gap <= 0) return {};

    const Trig& trig = trig_for_angle(angle);
    double ca = trig.cos_v;
    double sa = trig.sin_v;

    array<Point, 4> local = {{
        {0, (double)d},
        {(double)w, (double)d},
        {(double)w, (double)d + gap},
        {0, (double)d + gap}
    }};

    vector<Point> res;
    res.reserve(4);

    for (auto p : local) {
        double rx = p.x * ca - p.y * sa;
        double ry = p.x * sa + p.y * ca;

        res.push_back({x + rx, y + ry});
    }

    return res;
}

const vector<Point>& bay_poly(const PlacedBay& p) {
    return p.bay_pts;
}

const vector<Point>& gap_poly(const PlacedBay& p) {
    return p.gap_pts;
}

void refresh_cache(PlacedBay& p) {
    p.bay_pts = make_rotated_rect(p.x, p.y, p.w, p.d, p.angle);
    Bounds bb = polygon_bounds(p.bay_pts);
    p.bay_min_x = bb.min_x;
    p.bay_min_y = bb.min_y;
    p.bay_max_x = bb.max_x;
    p.bay_max_y = bb.max_y;
    if (p.gap > 0) {
        p.gap_pts = gap_poly_values(p.x, p.y, p.w, p.d, p.gap, p.angle);
        Bounds gb = polygon_bounds(p.gap_pts);
        p.gap_min_x = gb.min_x;
        p.gap_min_y = gb.min_y;
        p.gap_max_x = gb.max_x;
        p.gap_max_y = gb.max_y;
    } else {
        p.gap_pts.clear();
        p.gap_min_x = p.gap_min_y = p.gap_max_x = p.gap_max_y = 0;
    }
}

vector<Point> obstacle_poly(const Obstacle& o) {
    return {
        {o.x, o.y},
        {o.x + o.w, o.y},
        {o.x + o.w, o.y + o.d},
        {o.x, o.y + o.d}
    };
}

pair<double, double> minmax_x(const vector<Point>& poly) {
    double mn = poly[0].x;
    double mx = poly[0].x;

    for (auto p : poly) {
        mn = min(mn, p.x);
        mx = max(mx, p.x);
    }

    return {mn, mx};
}

// ================= CSV =================

vector<string> split_csv_line(string line) {
    vector<string> out;
    string cur;

    for (char c : line) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }

    out.push_back(cur);

    for (auto& s : out) {
        while (!s.empty() && isspace(s.front())) s.erase(s.begin());
        while (!s.empty() && isspace(s.back())) s.pop_back();
    }

    return out;
}

bool file_empty_or_missing(const string& path) {
    ifstream f(path);

    if (!f.good()) return true;

    string line;

    while (getline(f, line)) {
        for (char c : line) {
            if (!isspace(c)) return false;
        }
    }

    return true;
}

bool try_stod(const string& s, double& out) {
    if (s.empty()) return false;
    try { size_t pos = 0; out = stod(s, &pos); return pos > 0; }
    catch (...) { return false; }
}

bool try_stoi(const string& s, int& out) {
    if (s.empty()) return false;
    try { size_t pos = 0; out = stoi(s, &pos); return pos > 0; }
    catch (...) { return false; }
}

vector<Point> read_warehouse(const string& path) {
    vector<Point> res;
    ifstream f(path);
    string line;

    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() < 2) continue;
        double a, b;
        if (!try_stod(v[0], a) || !try_stod(v[1], b)) continue;
        res.push_back({a, b});
    }

    return res;
}

vector<Obstacle> read_obstacles(const string& path) {
    vector<Obstacle> res;
    if (file_empty_or_missing(path)) return res;

    ifstream f(path);
    string line;

    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() < 4) continue;
        double a, b, c, d;
        if (!try_stod(v[0], a) || !try_stod(v[1], b) ||
            !try_stod(v[2], c) || !try_stod(v[3], d)) continue;
        res.push_back({a, b, c, d});
    }

    return res;
}

vector<pair<double, double>> read_ceiling(const string& path) {
    vector<pair<double, double>> res;
    ifstream f(path);
    string line;

    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() < 2) continue;
        double a, b;
        if (!try_stod(v[0], a) || !try_stod(v[1], b)) continue;
        res.push_back({a, b});
    }

    sort(res.begin(), res.end());

    return res;
}

vector<BayType> read_bays(const string& path) {
    vector<BayType> res;
    ifstream f(path);
    string line;

    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() < 7) continue;
        int id, w, d, h, gap, loads, price;
        if (!try_stoi(v[0], id) || !try_stoi(v[1], w) || !try_stoi(v[2], d) ||
            !try_stoi(v[3], h) || !try_stoi(v[4], gap) || !try_stoi(v[5], loads) ||
            !try_stoi(v[6], price)) continue;
        res.push_back({id, w, d, h, gap, loads, price});
    }

    return res;
}

// ================= VALIDATION =================

double min_ceiling_between(double x1, double x2, const vector<pair<double, double>>& ceiling) {
    if (ceiling.empty()) return 1e18;

    if (x1 > x2) swap(x1, x2);

    double ans = 1e18;

    for (int i = 0; i < (int)ceiling.size(); i++) {
        double seg_x1 = ceiling[i].first;
        double h = ceiling[i].second;
        double seg_x2 = 1e18;

        if (i + 1 < (int)ceiling.size()) {
            seg_x2 = ceiling[i + 1].first;
        }

        double left = max(x1, seg_x1);
        double right = min(x2, seg_x2);

        if (left < right + EPS) {
            ans = min(ans, h);
        }
    }

    return ans;
}

struct SpatialIndex {
    double cell_size = 0;
    double origin_x = 0, origin_y = 0;
    int nx = 0, ny = 0;
    vector<vector<int>> cells;
    mutable vector<int> seen_gen;
    mutable int cur_gen = 0;

    void insert_bb(int idx, double xmin, double ymin, double xmax, double ymax) {
        int cx0 = max(0, (int)floor((xmin - origin_x) / cell_size));
        int cx1 = min(nx - 1, (int)floor((xmax - origin_x) / cell_size));
        int cy0 = max(0, (int)floor((ymin - origin_y) / cell_size));
        int cy1 = min(ny - 1, (int)floor((ymax - origin_y) / cell_size));
        for (int cy = cy0; cy <= cy1; cy++) {
            for (int cx = cx0; cx <= cx1; cx++) {
                cells[cy * nx + cx].push_back(idx);
            }
        }
    }

    void build(const vector<PlacedBay>& sol) {
        cells.clear();
        cell_size = 0;
        if (sol.empty()) return;

        double mnx = sol[0].bay_min_x, mxx = sol[0].bay_max_x;
        double mny = sol[0].bay_min_y, mxy = sol[0].bay_max_y;
        double max_extent = 0;

        for (auto& p : sol) {
            mnx = min(mnx, p.bay_min_x);
            mxx = max(mxx, p.bay_max_x);
            mny = min(mny, p.bay_min_y);
            mxy = max(mxy, p.bay_max_y);
            if (p.gap > 0) {
                mnx = min(mnx, p.gap_min_x);
                mxx = max(mxx, p.gap_max_x);
                mny = min(mny, p.gap_min_y);
                mxy = max(mxy, p.gap_max_y);
            }
            max_extent = max(max_extent, max(p.bay_max_x - p.bay_min_x, p.bay_max_y - p.bay_min_y));
        }

        cell_size = max(2000.0, max_extent);
        origin_x = mnx - cell_size;
        origin_y = mny - cell_size;
        double width = (mxx - origin_x) + cell_size;
        double height = (mxy - origin_y) + cell_size;
        nx = max(1, (int)ceil(width / cell_size));
        ny = max(1, (int)ceil(height / cell_size));
        cells.assign((size_t)nx * ny, {});

        for (int i = 0; i < (int)sol.size(); i++) {
            insert_bb(i, sol[i].bay_min_x, sol[i].bay_min_y, sol[i].bay_max_x, sol[i].bay_max_y);
            if (sol[i].gap > 0) {
                insert_bb(i, sol[i].gap_min_x, sol[i].gap_min_y, sol[i].gap_max_x, sol[i].gap_max_y);
            }
        }

        seen_gen.assign(sol.size(), 0);
        cur_gen = 0;
    }

    void query(double xmin, double ymin, double xmax, double ymax, vector<int>& out) const {
        out.clear();
        if (cells.empty()) return;
        cur_gen++;
        int cx0 = max(0, (int)floor((xmin - origin_x) / cell_size));
        int cx1 = min(nx - 1, (int)floor((xmax - origin_x) / cell_size));
        int cy0 = max(0, (int)floor((ymin - origin_y) / cell_size));
        int cy1 = min(ny - 1, (int)floor((ymax - origin_y) / cell_size));
        for (int cy = cy0; cy <= cy1; cy++) {
            for (int cx = cx0; cx <= cx1; cx++) {
                for (int idx : cells[cy * nx + cx]) {
                    if (seen_gen[idx] != cur_gen) {
                        seen_gen[idx] = cur_gen;
                        out.push_back(idx);
                    }
                }
            }
        }
    }
};

inline bool bb_disjoint_v(double a_min_x, double a_min_y, double a_max_x, double a_max_y,
                          double b_min_x, double b_min_y, double b_max_x, double b_max_y) {
    return a_max_x <= b_min_x + EPS || b_max_x <= a_min_x + EPS ||
           a_max_y <= b_min_y + EPS || b_max_y <= a_min_y + EPS;
}

bool valid_candidate(
    double x,
    double y,
    int w,
    int d,
    int h,
    int gap,
    int angle,
    const vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    int ignore = -1,
    const SpatialIndex* idx = nullptr
) {
    vector<Point> candidate_bay = make_rotated_rect(x, y, w, d, angle);
    vector<Point> candidate_gap = gap_poly_values(x, y, w, d, gap, angle);

    Bounds cand_bb = polygon_bounds(candidate_bay);
    Bounds gap_bb = candidate_gap.empty() ? cand_bb : polygon_bounds(candidate_gap);

    if (!polygon_inside_polygon(candidate_bay, warehouse)) return false;

    if (!candidate_gap.empty()) {
        if (!polygon_inside_polygon(candidate_gap, warehouse)) return false;
    }

    for (auto& o : obstacles) {
        // axis-aligned obstacle: cheap bb check
        double omx = o.x, omy = o.y, oxx = o.x + o.w, oyy = o.y + o.d;
        bool bay_disjoint = bb_disjoint_v(cand_bb.min_x, cand_bb.min_y, cand_bb.max_x, cand_bb.max_y,
                                          omx, omy, oxx, oyy);
        if (!bay_disjoint) {
            auto op = obstacle_poly(o);
            if (polygons_overlap_area(candidate_bay, op)) return false;
        }
        if (!candidate_gap.empty()) {
            bool gap_disjoint = bb_disjoint_v(gap_bb.min_x, gap_bb.min_y, gap_bb.max_x, gap_bb.max_y,
                                              omx, omy, oxx, oyy);
            if (!gap_disjoint) {
                auto op = obstacle_poly(o);
                if (polygons_overlap_area(candidate_gap, op)) return false;
            }
        }
    }

    auto check_against = [&](int i) -> bool {
        if (i == ignore) return true;
        const PlacedBay& other = sol[i];
        // bay vs other.bay
        if (!bb_disjoint_v(cand_bb.min_x, cand_bb.min_y, cand_bb.max_x, cand_bb.max_y,
                           other.bay_min_x, other.bay_min_y, other.bay_max_x, other.bay_max_y)) {
            if (polygons_overlap_area(candidate_bay, other.bay_pts)) return false;
        }
        // bay vs other.gap
        if (other.gap > 0 &&
            !bb_disjoint_v(cand_bb.min_x, cand_bb.min_y, cand_bb.max_x, cand_bb.max_y,
                           other.gap_min_x, other.gap_min_y, other.gap_max_x, other.gap_max_y)) {
            if (polygons_overlap_area(candidate_bay, other.gap_pts)) return false;
        }
        // gap vs other.bay
        if (!candidate_gap.empty() &&
            !bb_disjoint_v(gap_bb.min_x, gap_bb.min_y, gap_bb.max_x, gap_bb.max_y,
                           other.bay_min_x, other.bay_min_y, other.bay_max_x, other.bay_max_y)) {
            if (polygons_overlap_area(candidate_gap, other.bay_pts)) return false;
        }
        // gap-gap allowed
        return true;
    };

    if (idx && idx->cell_size > 0) {
        static thread_local vector<int> hits;
        double qmin_x = candidate_gap.empty() ? cand_bb.min_x : min(cand_bb.min_x, gap_bb.min_x);
        double qmin_y = candidate_gap.empty() ? cand_bb.min_y : min(cand_bb.min_y, gap_bb.min_y);
        double qmax_x = candidate_gap.empty() ? cand_bb.max_x : max(cand_bb.max_x, gap_bb.max_x);
        double qmax_y = candidate_gap.empty() ? cand_bb.max_y : max(cand_bb.max_y, gap_bb.max_y);
        idx->query(qmin_x, qmin_y, qmax_x, qmax_y, hits);
        for (int i : hits) {
            if (i >= (int)sol.size()) continue;
            if (!check_against(i)) return false;
        }
    } else {
        for (int i = 0; i < (int)sol.size(); i++) {
            if (!check_against(i)) return false;
        }
    }

    auto bx = minmax_x(candidate_bay);
    double min_h = min_ceiling_between(bx.first, bx.second, ceiling);

    if (h > min_h + EPS) return false;

    return true;
}

// ================= SCORE =================

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

struct QSums { double price = 0, loads = 0, area = 0; };

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

bool is_valid_solution(
    const vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling
) {
    for (int i = 0; i < (int)sol.size(); i++) {
        const auto& p = sol[i];

        if (!valid_candidate(
                p.x,
                p.y,
                p.w,
                p.d,
                p.h,
                p.gap,
                p.angle,
                sol,
                warehouse,
                obstacles,
                ceiling,
                i
            )) {
            cerr << "Invalid bay " << i << "\n";
            return false;
        }
    }

    return true;
}

double seconds_since(Clock::time_point t0) {
    return chrono::duration<double>(Clock::now() - t0).count();
}

// ============== CORNER POINTS ==============

struct CornerPoint {
    double x, y;
    enum Origin {
        WAREHOUSE_BBOX, CONCAVE_VERTEX,
        BAY_TL, BAY_TR, BAY_BL, BAY_BR,
        GAP_TL, GAP_TR, GAP_BL, GAP_BR,
        INTERIOR_GRID
    } origin;
};

static void ensure_ccw(vector<Point>& poly) {
    double s = 0.0;
    int n = (int)poly.size();
    for (int i = 0; i < n; i++) {
        const Point& a = poly[i];
        const Point& b = poly[(i + 1) % n];
        s += (b.x - a.x) * (b.y + a.y);
    }
    if (s > 0) reverse(poly.begin(), poly.end());
}

static bool is_concave_vertex_ccw(const vector<Point>& p, int i) {
    int n = (int)p.size();
    Point a = p[(i - 1 + n) % n];
    Point b = p[i];
    Point c = p[(i + 1) % n];
    return cross(a, b, c) < -EPS;
}

vector<CornerPoint> initial_corner_points(const vector<Point>& warehouse) {
    vector<CornerPoint> out;
    Bounds bb = polygon_bounds(warehouse);
    out.push_back({bb.min_x, bb.min_y, CornerPoint::WAREHOUSE_BBOX});
    out.push_back({bb.max_x, bb.min_y, CornerPoint::WAREHOUSE_BBOX});
    out.push_back({bb.min_x, bb.max_y, CornerPoint::WAREHOUSE_BBOX});
    out.push_back({bb.max_x, bb.max_y, CornerPoint::WAREHOUSE_BBOX});
    int n = (int)warehouse.size();
    for (int i = 0; i < n; i++) {
        if (is_concave_vertex_ccw(warehouse, i))
            out.push_back({warehouse[i].x, warehouse[i].y, CornerPoint::CONCAVE_VERTEX});
    }
    return out;
}

vector<CornerPoint> corners_from_placed_bay(const PlacedBay& p) {
    vector<CornerPoint> out;
    out.push_back({p.bay_min_x, p.bay_min_y, CornerPoint::BAY_BL});
    out.push_back({p.bay_max_x, p.bay_min_y, CornerPoint::BAY_BR});
    out.push_back({p.bay_min_x, p.bay_max_y, CornerPoint::BAY_TL});
    out.push_back({p.bay_max_x, p.bay_max_y, CornerPoint::BAY_TR});
    if (p.gap > 0) {
        out.push_back({p.gap_min_x, p.gap_min_y, CornerPoint::GAP_BL});
        out.push_back({p.gap_max_x, p.gap_min_y, CornerPoint::GAP_BR});
        out.push_back({p.gap_min_x, p.gap_max_y, CornerPoint::GAP_TL});
        out.push_back({p.gap_max_x, p.gap_max_y, CornerPoint::GAP_TR});
    }
    return out;
}

void filter_corners_inside_envelope(vector<CornerPoint>& corners, const PlacedBay& p) {
    double envx0 = (p.gap > 0) ? min(p.bay_min_x, p.gap_min_x) : p.bay_min_x;
    double envy0 = (p.gap > 0) ? min(p.bay_min_y, p.gap_min_y) : p.bay_min_y;
    double envx1 = (p.gap > 0) ? max(p.bay_max_x, p.gap_max_x) : p.bay_max_x;
    double envy1 = (p.gap > 0) ? max(p.bay_max_y, p.gap_max_y) : p.bay_max_y;
    auto strictly_inside = [&](const CornerPoint& c) {
        return c.x > envx0 + EPS && c.x < envx1 - EPS
            && c.y > envy0 + EPS && c.y < envy1 - EPS;
    };
    corners.erase(remove_if(corners.begin(), corners.end(), strictly_inside), corners.end());
}

// rotation: 0 -> angle=0; 1 -> angle=90
PlacedBay make_candidate_axis_aligned(const BayType& t, double x, double y, int rotation) {
    PlacedBay p{};
    p.id = t.id;
    p.x = x; p.y = y;
    p.w = t.w; p.d = t.d; p.h = t.h; p.gap = t.gap;
    p.loads = t.loads; p.price = t.price;
    p.angle = (rotation == 0) ? 0 : 90;
    refresh_cache(p);
    return p;
}

// Generates a regular grid of candidate points inside the warehouse polygon,
// excluding obstacle interiors. Cap at INTERIOR_GRID_CAP points.
vector<CornerPoint> interior_grid_points(
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<BayType>& types
) {
    if (types.empty()) return {};

    int min_dim = INT_MAX;
    for (auto& t : types) {
        min_dim = min(min_dim, min(t.w, t.d));
    }
    double step = max(500.0, min(5000.0, min_dim / 2.0));

    Bounds bb = polygon_bounds(warehouse);
    vector<CornerPoint> out;

    for (double x = bb.min_x; x <= bb.max_x + EPS && (int)out.size() < INTERIOR_GRID_CAP; x += step) {
        for (double y = bb.min_y; y <= bb.max_y + EPS && (int)out.size() < INTERIOR_GRID_CAP; y += step) {
            Point p{x, y};
            if (!point_inside_or_on_polygon(p, warehouse)) continue;
            bool in_obstacle = false;
            for (auto& o : obstacles) {
                if (x > o.x + EPS && x < o.x + o.w - EPS &&
                    y > o.y + EPS && y < o.y + o.d - EPS) {
                    in_obstacle = true;
                    break;
                }
            }
            if (!in_obstacle) {
                out.push_back({x, y, CornerPoint::INTERIOR_GRID});
            }
        }
    }
    return out;
}

// Counts how many valid (type, rotation) placements exist at any corner point
// within `radius` of `cand`'s bounding box center, given sol already contains cand.
int count_neighbors(
    const vector<PlacedBay>& sol_with_cand,
    const PlacedBay& cand,
    double radius,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling
) {
    double cx = (cand.bay_min_x + cand.bay_max_x) / 2.0;
    double cy = (cand.bay_min_y + cand.bay_max_y) / 2.0;

    auto cand_corners = corners_from_placed_bay(cand);
    auto wh_corners = initial_corner_points(warehouse);
    for (auto& wc : wh_corners) cand_corners.push_back(wc);

    SpatialIndex idx;
    idx.build(sol_with_cand);

    int count = 0;
    for (auto& cp : cand_corners) {
        double dx = cp.x - cx, dy = cp.y - cy;
        if (dx*dx + dy*dy > radius*radius) continue;
        for (auto& t : types) {
            for (int rot = 0; rot < 2; rot++) {
                PlacedBay nb = make_candidate_axis_aligned(t, cp.x, cp.y, rot);
                if (valid_candidate(nb.x, nb.y, nb.w, nb.d, nb.h, nb.gap,
                                    nb.angle, sol_with_cand, warehouse,
                                    obstacles, ceiling, -1, &idx)) {
                    count++;
                }
            }
        }
    }
    return count;
}

// ============== VARIANT + SCORE ==============

enum class SweepOrder { LOWEST_LEFT, LEFTMOST_LOW, LONGEST_EDGE_FIRST };
enum class TypePrio   { BY_PRICE_PER_LOAD, BY_AREA_DESC, BY_AREA_ASC, BY_DENSITY };

struct Variant {
    SweepOrder sweep;
    TypePrio   type_prio;
    int        seed_corner;   // 0=BL,1=BR,2=TL,3=TR
    double w_ratio, w_fill, w_gap_pair, w_corner, w_ceiling;
    string name;
};

struct ScoreNorms {
    double max_price_per_load;
    double max_bay_area;
    double max_ceiling;
};

ScoreNorms compute_norms(const vector<BayType>& types,
                         const vector<pair<double,double>>& ceiling) {
    ScoreNorms n{0,0,0};
    for (auto& t : types) {
        double pr = t.price / max(1, t.loads);
        if (pr > n.max_price_per_load) n.max_price_per_load = pr;
        double a = (double)t.w * t.d;
        if (a > n.max_bay_area) n.max_bay_area = a;
    }
    for (auto& s : ceiling) if (s.second > n.max_ceiling) n.max_ceiling = s.second;
    if (n.max_price_per_load <= 0) n.max_price_per_load = 1.0;
    if (n.max_bay_area <= 0) n.max_bay_area = 1.0;
    if (n.max_ceiling <= 0) n.max_ceiling = 1.0;
    return n;
}

int count_touching_sides(const PlacedBay& cand,
                         const vector<PlacedBay>& sol,
                         const vector<Point>& warehouse,
                         const vector<Obstacle>& obstacles) {
    int touches = 0;
    Bounds wb = polygon_bounds(warehouse);
    if (fabs(cand.bay_min_x - wb.min_x) < EPS) touches++;
    if (fabs(cand.bay_max_x - wb.max_x) < EPS) touches++;
    if (fabs(cand.bay_min_y - wb.min_y) < EPS) touches++;
    if (fabs(cand.bay_max_y - wb.max_y) < EPS) touches++;
    for (auto& o : obstacles) {
        double ox0=o.x, oy0=o.y, ox1=o.x+o.w, oy1=o.y+o.d;
        auto x_overlap = [&](double bx0,double bx1){ return !(bx1<ox0+EPS||bx0>ox1-EPS); };
        auto y_overlap = [&](double by0,double by1){ return !(by1<oy0+EPS||by0>oy1-EPS); };
        if (fabs(cand.bay_max_x-ox0)<EPS && y_overlap(cand.bay_min_y,cand.bay_max_y)) touches++;
        if (fabs(cand.bay_min_x-ox1)<EPS && y_overlap(cand.bay_min_y,cand.bay_max_y)) touches++;
        if (fabs(cand.bay_max_y-oy0)<EPS && x_overlap(cand.bay_min_x,cand.bay_max_x)) touches++;
        if (fabs(cand.bay_min_y-oy1)<EPS && x_overlap(cand.bay_min_x,cand.bay_max_x)) touches++;
    }
    for (auto& other : sol) {
        auto x_ov=[&](double b0,double b1){return !(b1<other.bay_min_x+EPS||b0>other.bay_max_x-EPS);};
        auto y_ov=[&](double b0,double b1){return !(b1<other.bay_min_y+EPS||b0>other.bay_max_y-EPS);};
        if (fabs(cand.bay_max_x-other.bay_min_x)<EPS && y_ov(cand.bay_min_y,cand.bay_max_y)) touches++;
        if (fabs(cand.bay_min_x-other.bay_max_x)<EPS && y_ov(cand.bay_min_y,cand.bay_max_y)) touches++;
        if (fabs(cand.bay_max_y-other.bay_min_y)<EPS && x_ov(cand.bay_min_x,cand.bay_max_x)) touches++;
        if (fabs(cand.bay_min_y-other.bay_max_y)<EPS && x_ov(cand.bay_min_x,cand.bay_max_x)) touches++;
    }
    return touches;
}

bool has_shared_gap(const PlacedBay& cand,
                    const vector<PlacedBay>& sol,
                    const vector<Point>& warehouse) {
    if (cand.gap == 0) return false;
    Bounds wb = polygon_bounds(warehouse);
    if (cand.angle == 0) {
        // gap is on +y side of bay
        if (fabs(cand.gap_max_y - wb.max_y) < EPS) return true;
        for (auto& o : sol) {
            if (o.angle == 0 && o.gap > 0 &&
                fabs(o.bay_min_y - cand.gap_max_y) < EPS &&
                !(o.bay_max_x < cand.bay_min_x+EPS || o.bay_min_x > cand.bay_max_x-EPS))
                return true;
        }
    } else { // angle==90, gap on +x side
        if (fabs(cand.gap_max_x - wb.max_x) < EPS) return true;
        for (auto& o : sol) {
            if (o.angle == 90 && o.gap > 0 &&
                fabs(o.bay_min_x - cand.gap_max_x) < EPS &&
                !(o.bay_max_y < cand.bay_min_y+EPS || o.bay_min_y > cand.bay_max_y-EPS))
                return true;
        }
    }
    return false;
}

double score_candidate(const PlacedBay& cand,
                       const Variant& v,
                       const ScoreNorms& norms,
                       const vector<PlacedBay>& sol,
                       const vector<Point>& warehouse,
                       const vector<Obstacle>& obstacles,
                       const vector<pair<double,double>>& ceiling) {
    // Lower price/loads is better -> invert with (1 - S_ratio)
    double S_ratio = (cand.price / max(1, cand.loads)) / norms.max_price_per_load;
    double S_fill  = ((double)cand.w * cand.d) / norms.max_bay_area;
    double S_gap   = has_shared_gap(cand, sol, warehouse) ? 1.0 : 0.0;
    int t = count_touching_sides(cand, sol, warehouse, obstacles);
    double S_corner = (t >= 2) ? 1.0 : (t == 1 ? 0.5 : 0.0);
    double local_ceil = min_ceiling_between(cand.bay_min_x, cand.bay_max_x, ceiling);
    double slack = max(0.0, local_ceil - cand.h);
    double S_ceil = max(0.0, min(1.0, 1.0 - slack / norms.max_ceiling));
    return v.w_ratio    * (1.0 - S_ratio)
         + v.w_fill     * S_fill
         + v.w_gap_pair * S_gap
         + v.w_corner   * S_corner
         + v.w_ceiling  * S_ceil;
}

// ============== SWEEP + SORT ==============

struct EdgeSeed { Point origin; double dx, dy, length; };
EdgeSeed longest_edge_seed(const vector<Point>& warehouse) {
    int n = (int)warehouse.size();
    EdgeSeed best{{0,0},1,0,0};
    for (int i = 0; i < n; i++) {
        Point a = warehouse[i], b = warehouse[(i+1)%n];
        double dx=b.x-a.x, dy=b.y-a.y, L=sqrt(dx*dx+dy*dy);
        if (L > best.length) {
            Point o = (a.x+a.y <= b.x+b.y) ? a : b;
            best = {o, dx, dy, L};
        }
    }
    return best;
}

void sort_corners(vector<CornerPoint>& cps, const Variant& v, const vector<Point>& warehouse) {
    bool flip_x = (v.seed_corner==1||v.seed_corner==3);
    bool flip_y = (v.seed_corner==2||v.seed_corner==3);
    if (v.sweep == SweepOrder::LOWEST_LEFT) {
        sort(cps.begin(),cps.end(),[&](const CornerPoint& a,const CornerPoint& b){
            double ya=flip_y?-a.y:a.y, yb=flip_y?-b.y:b.y;
            if (fabs(ya-yb)>EPS) return ya<yb;
            double xa=flip_x?-a.x:a.x, xb=flip_x?-b.x:b.x;
            return xa<xb;
        });
    } else if (v.sweep == SweepOrder::LEFTMOST_LOW) {
        sort(cps.begin(),cps.end(),[&](const CornerPoint& a,const CornerPoint& b){
            double xa=flip_x?-a.x:a.x, xb=flip_x?-b.x:b.x;
            if (fabs(xa-xb)>EPS) return xa<xb;
            double ya=flip_y?-a.y:a.y, yb=flip_y?-b.y:b.y;
            return ya<yb;
        });
    } else { // LONGEST_EDGE_FIRST
        EdgeSeed s = longest_edge_seed(warehouse);
        double L = max(s.length,1.0);
        double tx=s.dx/L, ty=s.dy/L, nx=-ty, ny=tx;
        sort(cps.begin(),cps.end(),[&](const CornerPoint& a,const CornerPoint& b){
            double ta=(a.x-s.origin.x)*tx+(a.y-s.origin.y)*ty;
            double tb=(b.x-s.origin.x)*tx+(b.y-s.origin.y)*ty;
            if (fabs(ta-tb)>EPS) return ta<tb;
            double na=(a.x-s.origin.x)*nx+(a.y-s.origin.y)*ny;
            double nb=(b.x-s.origin.x)*nx+(b.y-s.origin.y)*ny;
            return na<nb;
        });
    }
}

vector<int> sort_types(const vector<BayType>& types, TypePrio p) {
    vector<int> idx(types.size());
    iota(idx.begin(),idx.end(),0);
    auto pr=[&](int i){return types[i].price/max(1,types[i].loads);};
    auto ar=[&](int i){return (double)types[i].w*types[i].d;};
    auto den=[&](int i){return pr(i)/max(1.0,ar(i));};
    sort(idx.begin(),idx.end(),[&](int a,int b){
        switch(p){
            case TypePrio::BY_PRICE_PER_LOAD: return pr(a)<pr(b);
            case TypePrio::BY_AREA_DESC: return ar(a)>ar(b);
            case TypePrio::BY_AREA_ASC: return ar(a)<ar(b);
            case TypePrio::BY_DENSITY: return den(a)<den(b);
        }
        return false;
    });
    return idx;
}

// ============== CONSTRUCTION LOOP ==============

vector<PlacedBay> construct_variant(
    const Variant& v,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area
) {
    ScoreNorms norms = compute_norms(types, ceiling);
    vector<int> type_order = sort_types(types, v.type_prio);
    vector<PlacedBay> sol;
    vector<CornerPoint> corners = initial_corner_points(warehouse);
    // Add interior grid points once at the start (not regenerated per iteration).
    auto grid_pts = interior_grid_points(warehouse, obstacles, types);
    for (auto& gp : grid_pts) corners.push_back(gp);

    int iteration = 0;
    int N_relax = max(5, (int)types.size());
    int max_bay_dim = 1;
    for (auto& t : types) max_bay_dim = max(max_bay_dim, max(t.w + t.gap, t.d + t.gap));
    double lookahead_radius = 2.0 * max_bay_dim;

    while (true) {
        sort_corners(corners, v, warehouse);
        QSums sums = compute_sums(sol);
        double Q_now = sol.empty() ? 1e100 : quality_from_sums(sums, wh_area, false);

        struct Best { double score; int cp_idx; int ti; int rot; PlacedBay bay; };
        Best best{-1e100, -1, -1, -1, {}};

        SpatialIndex idx;
        idx.build(sol);

        bool use_relax = (!sol.empty() && iteration < N_relax);

        for (int ci = 0; ci < (int)corners.size(); ci++) {
            const CornerPoint& cp = corners[ci];
            for (int ti : type_order) {
                const BayType& t = types[ti];
                for (int rot = 0; rot < 2; rot++) {
                    PlacedBay cand = make_candidate_axis_aligned(t, cp.x, cp.y, rot);
                    if (!valid_candidate(cand.x,cand.y,cand.w,cand.d,cand.h,cand.gap,
                                         cand.angle,sol,warehouse,obstacles,ceiling,-1,&idx))
                        continue;
                    double bay_area = (double)cand.w * cand.d;
                    double Q_new = quality_with_added(sums, cand.price, cand.loads, bay_area, wh_area);
                    bool passes_q;
                    if (sol.empty()) {
                        passes_q = true;
                    } else if (use_relax) {
                        passes_q = (Q_new <= Q_now * (1.0 + RELAX_FACTOR));
                    } else {
                        passes_q = (Q_new < Q_now - EPS);
                    }
                    if (!passes_q) continue;
                    double s = score_candidate(cand,v,norms,sol,warehouse,obstacles,ceiling);
                    bool win = (s > best.score + EPS);
                    if (!win && fabs(s - best.score) <= EPS) {
                        if (best.cp_idx < 0) win = true;
                        else if (cp.x+cp.y < corners[best.cp_idx].x+corners[best.cp_idx].y - EPS) win = true;
                        else if (fabs(cp.x+cp.y-(corners[best.cp_idx].x+corners[best.cp_idx].y)) <= EPS) {
                            if (t.id < best.bay.id) win = true;
                            else if (t.id == best.bay.id && rot < best.rot) win = true;
                        }
                    }
                    if (win) best = {s, ci, ti, rot, cand};
                }
            }
        }

        if (best.cp_idx < 0) break;

        sol.push_back(best.bay);
        auto new_corners = corners_from_placed_bay(best.bay);
        for (auto& nc : new_corners) corners.push_back(nc);
        filter_corners_inside_envelope(corners, best.bay);
        iteration++;
    }

    return sol;
}

// ============== VARIANTS TABLE ==============

vector<Variant> all_variants() {
    using SO = SweepOrder; using TP = TypePrio;
    return {
        {SO::LOWEST_LEFT,        TP::BY_PRICE_PER_LOAD, 0, 1.0, 0.5, 0.3, 0.3, 0.2, "v01_LL_PPL_BL"    },
        {SO::LOWEST_LEFT,        TP::BY_AREA_DESC,      0, 0.5, 1.0, 0.3, 0.3, 0.2, "v02_LL_AD_BL"     },
        {SO::LOWEST_LEFT,        TP::BY_AREA_ASC,       0, 0.7, 0.7, 0.5, 0.3, 0.2, "v03_LL_AA_BL"     },
        {SO::LEFTMOST_LOW,       TP::BY_PRICE_PER_LOAD, 0, 1.0, 0.5, 0.3, 0.3, 0.2, "v04_LML_PPL_BL"   },
        {SO::LEFTMOST_LOW,       TP::BY_DENSITY,        0, 0.7, 0.7, 0.3, 0.3, 0.2, "v05_LML_DEN_BL"   },
        {SO::LONGEST_EDGE_FIRST, TP::BY_PRICE_PER_LOAD, 0, 1.0, 0.5, 0.5, 0.3, 0.2, "v06_LE_PPL"       },
        {SO::LONGEST_EDGE_FIRST, TP::BY_AREA_DESC,      0, 0.5, 1.0, 0.5, 0.3, 0.2, "v07_LE_AD"        },
        {SO::LOWEST_LEFT,        TP::BY_PRICE_PER_LOAD, 1, 1.0, 0.5, 0.3, 0.3, 0.2, "v08_LL_PPL_BR"    },
        {SO::LOWEST_LEFT,        TP::BY_PRICE_PER_LOAD, 2, 1.0, 0.5, 0.3, 0.3, 0.2, "v09_LL_PPL_TL"    },
        {SO::LOWEST_LEFT,        TP::BY_PRICE_PER_LOAD, 3, 1.0, 0.5, 0.3, 0.3, 0.2, "v10_LL_PPL_TR"    },
        {SO::LOWEST_LEFT,        TP::BY_AREA_DESC,      0, 1.0, 1.0, 0.5, 0.5, 0.5, "v11_LL_AD_BL_aggr"},
        {SO::LONGEST_EDGE_FIRST, TP::BY_AREA_ASC,       0, 0.5, 0.5, 1.0, 0.5, 0.2, "v12_LE_AA_gap"    },
    };
}

// ============== IMPROVEMENT OPERATORS ==============

bool upgrade_pass(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area
) {
    bool improved = false;
    QSums sums = compute_sums(sol);
    for (int i = 0; i < (int)sol.size(); i++) {
        double Q_best = quality_from_sums(sums, wh_area, false);
        PlacedBay best_bay = sol[i];
        QSums best_sums = sums;
        for (auto& t : types) {
            for (int rot = 0; rot < 2; rot++) {
                PlacedBay cand = make_candidate_axis_aligned(t, sol[i].x, sol[i].y, rot);
                if (!valid_candidate(cand.x, cand.y, cand.w, cand.d, cand.h, cand.gap,
                                     cand.angle, sol, warehouse, obstacles, ceiling, i))
                    continue;
                QSums trial = sums;
                trial.price -= sol[i].price;
                trial.loads -= sol[i].loads;
                trial.area  -= (double)sol[i].w * sol[i].d;
                trial.price += cand.price;
                trial.loads += cand.loads;
                trial.area  += (double)cand.w * cand.d;
                double Q_new = quality_from_sums(trial, wh_area, false);
                if (Q_new < Q_best - EPS) {
                    Q_best = Q_new;
                    best_bay = cand;
                    best_sums = trial;
                }
            }
        }
        if (best_bay.id != sol[i].id || best_bay.angle != sol[i].angle ||
            best_bay.w != sol[i].w || best_bay.d != sol[i].d) {
            sol[i] = best_bay;
            sums = best_sums;
            improved = true;
        }
    }
    return improved;
}

bool fill_pass(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area
) {
    if (types.empty()) return false;

    ScoreNorms norms = compute_norms(types, ceiling);
    Variant fill_v{SweepOrder::LOWEST_LEFT, TypePrio::BY_PRICE_PER_LOAD, 0,
                   1.0, 0.5, 0.3, 0.3, 0.2, "fill"};
    vector<int> type_order = sort_types(types, fill_v.type_prio);

    vector<CornerPoint> corners = initial_corner_points(warehouse);
    for (auto& p : sol) {
        auto cp = corners_from_placed_bay(p);
        for (auto& c : cp) corners.push_back(c);
    }

    bool any_added = false;

    while (true) {
        sort_corners(corners, fill_v, warehouse);
        QSums sums = compute_sums(sol);
        double Q_now = sol.empty() ? 1e100 : quality_from_sums(sums, wh_area, false);

        struct Best { double score; int ci; int rot; PlacedBay bay; };
        Best best{-1e100, -1, -1, {}};

        SpatialIndex idx;
        idx.build(sol);

        for (int ci = 0; ci < (int)corners.size(); ci++) {
            const CornerPoint& cp = corners[ci];
            for (int ti : type_order) {
                const BayType& t = types[ti];
                for (int rot = 0; rot < 2; rot++) {
                    PlacedBay cand = make_candidate_axis_aligned(t, cp.x, cp.y, rot);
                    if (!valid_candidate(cand.x,cand.y,cand.w,cand.d,cand.h,cand.gap,
                                         cand.angle,sol,warehouse,obstacles,ceiling,-1,&idx))
                        continue;
                    double bay_area = (double)cand.w * cand.d;
                    double Q_new = quality_with_added(sums, cand.price, cand.loads, bay_area, wh_area);
                    if (!sol.empty() && Q_new >= Q_now - EPS) continue;
                    double s = score_candidate(cand,fill_v,norms,sol,warehouse,obstacles,ceiling);
                    bool win = (s > best.score + EPS);
                    if (!win && fabs(s - best.score) <= EPS) {
                        if (best.ci < 0) win = true;
                        else if (cp.x+cp.y < corners[best.ci].x+corners[best.ci].y - EPS) win = true;
                        else if (fabs(cp.x+cp.y-(corners[best.ci].x+corners[best.ci].y)) <= EPS) {
                            if (t.id < best.bay.id) win = true;
                            else if (t.id == best.bay.id && rot < best.rot) win = true;
                        }
                    }
                    if (win) best = {s, ci, rot, cand};
                }
            }
        }

        if (best.ci < 0) break;
        sol.push_back(best.bay);
        auto nc = corners_from_placed_bay(best.bay);
        for (auto& c : nc) corners.push_back(c);
        filter_corners_inside_envelope(corners, best.bay);
        any_added = true;
    }
    return any_added;
}

bool remove_k_refill(
    vector<PlacedBay>& sol,
    int k,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area
) {
    if ((int)sol.size() < k) return false;
    bool improved = false;

    QSums full_sums = compute_sums(sol);
    double Q_full = quality_from_sums(full_sums, wh_area, false);

    auto q_without = [&](int i) -> double {
        QSums s = full_sums;
        s.price -= sol[i].price;
        s.loads -= sol[i].loads;
        s.area  -= (double)sol[i].w * sol[i].d;
        if (s.price <= 0 && s.loads <= 0) return 1e100;
        return quality_from_sums(s, wh_area, s.price <= 0);
    };

    vector<int> order(sol.size());
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b){
        return q_without(a) < q_without(b);
    });

    if (k == 1) {
        for (int idx : order) {
            vector<PlacedBay> trial = sol;
            trial.erase(trial.begin() + idx);
            fill_pass(trial, types, warehouse, obstacles, ceiling, wh_area);
            double Q_trial = quality(trial, wh_area);
            if (Q_trial < Q_full - EPS) {
                sol = trial;
                full_sums = compute_sums(sol);
                Q_full = Q_trial;
                improved = true;
                order.resize(sol.size());
                iota(order.begin(), order.end(), 0);
                sort(order.begin(), order.end(), [&](int a, int b){
                    return q_without(a) < q_without(b);
                });
            }
        }
    } else if (k == 2) {
        vector<pair<int,int>> pairs;
        for (int i = 0; i < (int)sol.size() && (int)pairs.size() < 30; i++) {
            double cx_i = (sol[i].bay_min_x + sol[i].bay_max_x) / 2.0;
            double cy_i = (sol[i].bay_min_y + sol[i].bay_max_y) / 2.0;
            double best_dist = 1e18;
            int best_j = -1;
            for (int j = i+1; j < (int)sol.size(); j++) {
                double cx_j = (sol[j].bay_min_x + sol[j].bay_max_x) / 2.0;
                double cy_j = (sol[j].bay_min_y + sol[j].bay_max_y) / 2.0;
                double d = (cx_i-cx_j)*(cx_i-cx_j) + (cy_i-cy_j)*(cy_i-cy_j);
                if (d < best_dist) { best_dist = d; best_j = j; }
            }
            if (best_j >= 0) pairs.push_back({i, best_j});
        }
        for (auto [i, j] : pairs) {
            if (i >= (int)sol.size() || j >= (int)sol.size()) continue;
            vector<PlacedBay> trial = sol;
            int hi = max(i,j), lo = min(i,j);
            trial.erase(trial.begin() + hi);
            trial.erase(trial.begin() + lo);
            fill_pass(trial, types, warehouse, obstacles, ceiling, wh_area);
            double Q_trial = quality(trial, wh_area);
            if (Q_trial < Q_full - EPS) {
                sol = trial;
                Q_full = Q_trial;
                full_sums = compute_sums(sol);
                improved = true;
            }
        }
    } else { // k == 3
        if ((int)order.size() < 3) return false;
        vector<int> to_remove = {order[0], order[1], order[2]};
        sort(to_remove.rbegin(), to_remove.rend());
        vector<PlacedBay> trial = sol;
        for (int idx : to_remove) trial.erase(trial.begin() + idx);
        fill_pass(trial, types, warehouse, obstacles, ceiling, wh_area);
        double Q_trial = quality(trial, wh_area);
        if (Q_trial < Q_full - EPS) {
            sol = trial;
            improved = true;
        }
    }
    return improved;
}

void improve(
    vector<PlacedBay>& sol,
    Clock::time_point t_start,
    double budget_seconds,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area
) {
    while (seconds_since(t_start) < budget_seconds) {
        bool any = false;
        any |= upgrade_pass(sol, types, warehouse, obstacles, ceiling, wh_area);
        if (seconds_since(t_start) >= budget_seconds) break;
        any |= fill_pass(sol, types, warehouse, obstacles, ceiling, wh_area);
        if (seconds_since(t_start) >= budget_seconds) break;
        // remove_k_refill disabled for now — too slow for typical case sizes
        if (!any) break;
    }
}

void solve_case(const string& case_dir, double budget_per_case) {
    auto t0 = Clock::now();
    cout << "\n=== Solving " << case_dir << " ===\n";
    auto warehouse = read_warehouse(case_dir + "/warehouse.csv");
    auto obstacles = read_obstacles(case_dir + "/obstacles.csv");
    auto ceiling   = read_ceiling(case_dir + "/ceiling.csv");
    auto types     = read_bays(case_dir + "/types_of_bays.csv");
    ensure_ccw(warehouse);
    double wh_area = available_warehouse_area(warehouse, obstacles);
    auto variants = all_variants();
    int N = (int)variants.size();
    vector<vector<PlacedBay>> sols(N);
    vector<double> qs(N, 1e100);

    #pragma omp parallel for schedule(dynamic,1)
    for (int i = 0; i < N; i++) {
        auto t_v = Clock::now();
        sols[i] = construct_variant(variants[i], types, warehouse, obstacles, ceiling, wh_area);
        improve(sols[i], t_v, budget_per_case, types, warehouse, obstacles, ceiling, wh_area);
        auto [a,l,p,q] = details(sols[i], wh_area);
        qs[i] = q;
    }

    int best = 0;
    for (int i = 1; i < N; i++) if (qs[i] < qs[best]) best = i;

    if (!is_valid_solution(sols[best], warehouse, obstacles, ceiling)) {
        cerr << "WARNING: best solution for " << case_dir << " failed validity check; emitting empty.\n";
        sols[best].clear();
        qs[best] = 1e100;
    }

    for (int i = 0; i < N; i++)
        cout << "  " << variants[i].name << " bays=" << sols[i].size()
             << " Q=" << qs[i] << (i==best?" <-- BEST":"") << "\n";

    string out_path = case_dir + "/solution.csv";
    ofstream out(out_path);
    out << "Id,X,Y,Rotation\n";
    for (auto& p : sols[best])
        out << p.id << "," << llround(p.x) << "," << llround(p.y) << ","
            << (p.angle==0?0:1) << "\n";
    out.close();
    cout << "BEST " << case_dir << " -> " << variants[best].name
         << " Q=" << qs[best] << " bays=" << sols[best].size()
         << " [" << seconds_since(t0) << "s]\n";
}

int main() {
    auto t0 = Clock::now();

    int n_existing = 0;
    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");
        if (f.good()) n_existing++;
    }
    double budget_per_case = TOTAL_BUDGET_SECONDS / max(1, n_existing);
    cout << "cases_found=" << n_existing
         << " budget_per_case=" << budget_per_case << "s\n";

    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");
        if (f.good()) solve_case(c, budget_per_case);
        else cout << "Skipping " << c << "\n";
    }
    cout << "\n[time] total elapsed=" << seconds_since(t0) << "s\n";
    return 0;
}
