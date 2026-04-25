#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>
#include <fstream>
#include <tuple>
#include <numeric>
#include <string>
#include <future>
#include <array>
#include <chrono>

using namespace std;
using Clock = chrono::steady_clock;

const vector<string> CASES = {"Case0", "Case1", "Case2", "Case3"};

const int ITERATIONS = 450;
const int INITIAL_ADDS = 80;
const int RESTARTS = 10;
const int MAX_POINTS_ADD = 80;
const int ANGLE_SAMPLE = 14;
const double WALL_BUDGET = 70.0;  // seconds — leave margin for the 120s server timeout

const double EPS = 1e-7;
const double PI = acos(-1.0);

thread_local mt19937 rng(42);

double seconds_since(Clock::time_point start) {
    return chrono::duration<double>(Clock::now() - start).count();
}

enum InitMode {
    CHEAP_LOAD = 0,
    BIG_AREA = 1,
    LOW_GAP = 2,
    BALANCED = 3
};

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
};

struct Obstacle {
    double x, y, w, d;
};

struct Bounds {
    double min_x, max_x, min_y, max_y;
};

struct Trig {
    double cos_v, sin_v;
};

struct OperatorStats {
    long long tried[6] = {0, 0, 0, 0, 0, 0};
    long long improved[6] = {0, 0, 0, 0, 0, 0};
};

const vector<string> OP_NAMES = {
    "add_bay",
    "replace_bay",
    "fill_aggressive",
    "remove_k_and_refill",
    "shared_gap_refill",
    "upgrade_bay"
};

vector<int> ANGLES = {
    0, 10, 20, 30, 40, 50, 60, 70, 80, 90,
    100, 110, 120, 130, 140, 150, 160, 170,
    180, 190, 200, 210, 220, 230, 240, 250, 260, 270,
    280, 290, 300, 310, 320, 330, 340, 350
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

vector<Point> bay_poly(const PlacedBay& p) {
    return make_rotated_rect(p.x, p.y, p.w, p.d, p.angle);
}

vector<Point> gap_poly(const PlacedBay& p) {
    return gap_poly_values(p.x, p.y, p.w, p.d, p.gap, p.angle);
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

vector<Point> read_warehouse(const string& path) {
    vector<Point> res;
    ifstream f(path);
    string line;

    while (getline(f, line)) {
        if (line.empty()) continue;

        auto v = split_csv_line(line);

        if (v.size() >= 2) {
            res.push_back({stod(v[0]), stod(v[1])});
        }
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

        if (v.size() >= 4) {
            res.push_back({
                stod(v[0]),
                stod(v[1]),
                stod(v[2]),
                stod(v[3])
            });
        }
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

        if (v.size() >= 2) {
            res.push_back({stod(v[0]), stod(v[1])});
        }
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

        if (v.size() >= 7) {
            res.push_back({
                stoi(v[0]),
                stoi(v[1]),
                stoi(v[2]),
                stoi(v[3]),
                stoi(v[4]),
                stoi(v[5]),
                stoi(v[6])
            });
        }
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
    int ignore = -1
) {
    vector<Point> candidate_bay = make_rotated_rect(x, y, w, d, angle);
    vector<Point> candidate_gap = gap_poly_values(x, y, w, d, gap, angle);

    if (!polygon_inside_polygon(candidate_bay, warehouse)) return false;

    if (!candidate_gap.empty()) {
        if (!polygon_inside_polygon(candidate_gap, warehouse)) return false;
    }

    for (auto& o : obstacles) {
        auto op = obstacle_poly(o);

        if (polygons_overlap_area(candidate_bay, op)) return false;

        if (!candidate_gap.empty() && polygons_overlap_area(candidate_gap, op)) {
            return false;
        }
    }

    for (int i = 0; i < (int)sol.size(); i++) {
        if (i == ignore) continue;

        vector<Point> other_bay = bay_poly(sol[i]);
        vector<Point> other_gap = gap_poly(sol[i]);

        if (polygons_overlap_area(candidate_bay, other_bay)) return false;

        if (!other_gap.empty() && polygons_overlap_area(candidate_bay, other_gap)) {
            return false;
        }

        if (!candidate_gap.empty() && polygons_overlap_area(candidate_gap, other_bay)) {
            return false;
        }

        // gap-gap allowed
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

double used_area(const vector<PlacedBay>& sol) {
    double area = 0;

    for (auto& p : sol) {
        area += (double)p.w * p.d;
    }

    return area;
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

// ================= OPERATORS =================

vector<Point> candidate_points(
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<PlacedBay>& sol
) {
    vector<Point> pts;

    for (auto p : warehouse) pts.push_back(p);

    for (auto& o : obstacles) {
        pts.push_back({o.x, o.y});
        pts.push_back({o.x + o.w, o.y});
        pts.push_back({o.x, o.y + o.d});
        pts.push_back({o.x + o.w, o.y + o.d});
    }

    for (auto& p : sol) {
        auto bp = bay_poly(p);
        auto gp = gap_poly(p);

        for (auto q : bp) pts.push_back({round(q.x), round(q.y)});
        for (auto q : gp) pts.push_back({round(q.x), round(q.y)});
    }

    shuffle(pts.begin(), pts.end(), rng);

    if ((int)pts.size() > 300) pts.resize(300);

    return pts;
}

PlacedBay make_candidate(const BayType& t, double x, double y, int angle) {
    return {
        t.id,
        x,
        y,
        t.w,
        t.d,
        t.h,
        t.gap,
        angle,
        t.price,
        t.loads
    };
}

bool add_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
) {
    auto pts = candidate_points(warehouse, obstacles, sol);

    vector<BayType> sorted_types = types;

    sort(sorted_types.begin(), sorted_types.end(), [&](const BayType& a, const BayType& b) {
        return bay_score_mode(a, mode, wh_area) > bay_score_mode(b, mode, wh_area);
    });

    PlacedBay best;
    bool found = false;
    double best_s = -1e100;

    int point_limit = min(MAX_POINTS_ADD, (int)pts.size());

    for (int pi = 0; pi < point_limit; pi++) {
        double x = pts[pi].x;
        double y = pts[pi].y;

        for (auto& t : sorted_types) {
            vector<int> angles = ANGLES;
            shuffle(angles.begin(), angles.end(), rng);

            int limit = min(ANGLE_SAMPLE, (int)angles.size());

            for (int ai = 0; ai < limit; ai++) {
                int angle = angles[ai];

                if (valid_candidate(
                        x,
                        y,
                        t.w,
                        t.d,
                        t.h,
                        t.gap,
                        angle,
                        sol,
                        warehouse,
                        obstacles,
                        ceiling
                    )) {
                    double s = candidate_score_mode(t, x, y, angle, wh_area, mode);

                    if (s > best_s) {
                        best_s = s;
                        best = make_candidate(t, x, y, angle);
                        found = true;
                    }
                }
            }
        }
    }

    if (found) {
        sol.push_back(best);
        return true;
    }

    return false;
}

void fill_aggressive(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
) {
    for (int i = 0; i < 5; i++) {
        if (!add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode)) {
            break;
        }
    }
}

void remove_k_and_refill(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
) {
    if (sol.empty()) return;

    vector<PlacedBay> backup = sol;

    int k = min(4, max(1, (int)sol.size() / 5));

    for (int i = 0; i < k && !sol.empty(); i++) {
        int idx = rng() % sol.size();
        sol.erase(sol.begin() + idx);
    }

    for (int i = 0; i < k + 3; i++) {
        add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode);
    }

    if (quality(sol, wh_area) > quality(backup, wh_area)) {
        sol = backup;
    }
}

void upgrade_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
) {
    if (sol.empty()) return;

    int idx = rng() % sol.size();
    PlacedBay old = sol[idx];

    sol.erase(sol.begin() + idx);

    PlacedBay best = old;
    double best_q = quality(vector<PlacedBay>{old}, wh_area);
    bool found = false;

    for (auto& t : types) {
        vector<int> angles = ANGLES;
        shuffle(angles.begin(), angles.end(), rng);

        int limit = min(ANGLE_SAMPLE, (int)angles.size());

        for (int i = 0; i < limit; i++) {
            int angle = angles[i];

            if (valid_candidate(
                    old.x,
                    old.y,
                    t.w,
                    t.d,
                    t.h,
                    t.gap,
                    angle,
                    sol,
                    warehouse,
                    obstacles,
                    ceiling
                )) {
                PlacedBay candidate = make_candidate(t, old.x, old.y, angle);

                sol.push_back(candidate);
                double q = quality(sol, wh_area);
                sol.pop_back();

                if (q < best_q) {
                    best_q = q;
                    best = candidate;
                    found = true;
                }
            }
        }
    }

    sol.push_back(found ? best : old);
}

void replace_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
) {
    if (sol.empty()) return;

    vector<PlacedBay> backup = sol;

    int idx = rng() % sol.size();
    sol.erase(sol.begin() + idx);

    bool ok = add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode);

    if (!ok || quality(sol, wh_area) > quality(backup, wh_area)) {
        sol = backup;
    }
}

void shared_gap_refill(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
) {
    if (sol.empty()) return;

    vector<PlacedBay> backup = sol;

    int k = min(6, max(2, (int)sol.size() / 4));

    for (int i = 0; i < k && !sol.empty(); i++) {
        int idx = rng() % sol.size();
        sol.erase(sol.begin() + idx);
    }

    vector<int> old_angles = ANGLES;

    ANGLES = {
        0, 180,
        90, 270,
        10, 190,
        80, 260,
        100, 280,
        170, 350
    };

    for (int i = 0; i < k + 5; i++) {
        add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode);
    }

    ANGLES = old_angles;

    if (quality(sol, wh_area) > quality(backup, wh_area)) {
        sol = backup;
    }
}

// ================= SOLVER =================

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

vector<PlacedBay> build_initial(
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
) {
    vector<PlacedBay> sol;

    for (int i = 0; i < INITIAL_ADDS; i++) {
        if (!add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode)) {
            break;
        }
    }

    return sol;
}

vector<PlacedBay> hill(
    vector<PlacedBay> sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode,
    OperatorStats& stats,
    Clock::time_point deadline
) {
    vector<PlacedBay> best = sol;
    double best_q = quality(best, wh_area);

    for (int it = 0; it < ITERATIONS; it++) {
        if (Clock::now() >= deadline) break;
        vector<PlacedBay> candidate = best;

        const vector<int> active_ops = {3, 4, 5};
        int op = active_ops[rng() % active_ops.size()];
        stats.tried[op]++;

        if (op == 0) {
            add_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }
        else if (op == 1) {
            replace_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }
        else if (op == 2) {
            fill_aggressive(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }
        else if (op == 3) {
            remove_k_and_refill(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }
        else if (op == 4) {
            shared_gap_refill(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }
        else {
            upgrade_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }

        double q = quality(candidate, wh_area);

        if (q < best_q) {
            best = candidate;
            best_q = q;
            stats.improved[op]++;
        }
    }

    return best;
}

void print_operator_stats(const string& case_dir, const OperatorStats& stats) {
    cout << "\nOperator stats for " << case_dir << ":\n";

    for (int i = 0; i < 6; i++) {
        double rate = 0.0;

        if (stats.tried[i] > 0) {
            rate = 100.0 * stats.improved[i] / stats.tried[i];
        }

        cout << OP_NAMES[i]
             << " | tried=" << stats.tried[i]
             << " | improved=" << stats.improved[i]
             << " | rate=" << rate << "%\n";
    }
}

void solve_case(const string& case_dir) {
    auto case_start = Clock::now();
    auto deadline = case_start + chrono::duration_cast<chrono::steady_clock::duration>(
        chrono::duration<double>(WALL_BUDGET));

    cout << "\n=== Solving " << case_dir << " ===\n";

    auto warehouse = read_warehouse(case_dir + "/warehouse.csv");
    auto obstacles = read_obstacles(case_dir + "/obstacles.csv");
    auto ceiling = read_ceiling(case_dir + "/ceiling.csv");
    auto types = read_bays(case_dir + "/types_of_bays.csv");

    double wh_area = polygon_area(warehouse);

    auto worker = [&](int r) {
        int mode = r % 4;

        rng.seed(42 + r * 1000 + (int)case_dir[4]);

        cout << "Restart " << r + 1 << "/" << RESTARTS
             << " started | mode=" << mode << "\n";

        auto sol = build_initial(types, warehouse, obstacles, ceiling, wh_area, mode);

        OperatorStats stats;

        sol = hill(sol, types, warehouse, obstacles, ceiling, wh_area, mode, stats, deadline);

        auto [af, lf, pf, qf] = details(sol, wh_area);

        bool valid = is_valid_solution(sol, warehouse, obstacles, ceiling);

        cout << "Restart " << r + 1
             << " -> bays=" << sol.size()
             << " area=" << af
             << " loads=" << lf
             << " price=" << pf
             << " Q=" << qf
             << " valid=" << valid
             << "\n";

        return make_pair(sol, stats);
    };

    vector<future<pair<vector<PlacedBay>, OperatorStats>>> futures;

    for (int r = 0; r < RESTARTS; r++) {
        futures.push_back(async(launch::async, worker, r));
    }

    vector<PlacedBay> best_global;
    double best_global_q = 1e100;
    OperatorStats total_stats;

    for (int r = 0; r < RESTARTS; r++) {
        auto result = futures[r].get();

        vector<PlacedBay> sol = result.first;
        OperatorStats stats = result.second;

        for (int i = 0; i < 6; i++) {
            total_stats.tried[i] += stats.tried[i];
            total_stats.improved[i] += stats.improved[i];
        }

        auto [a, l, pr, q] = details(sol, wh_area);
        bool valid = is_valid_solution(sol, warehouse, obstacles, ceiling);

        if (valid && q < best_global_q) {
            best_global = sol;
            best_global_q = q;
        }
    }

    if (!is_valid_solution(best_global, warehouse, obstacles, ceiling)) {
        best_global.clear();
    }

    string out_path = case_dir + "/solution.csv";
    ofstream out(out_path);

    out << "Id,X,Y,Rotation\n";

    for (auto& p : best_global) {
        out << p.id << ","
            << llround(p.x) << ","
            << llround(p.y) << ","
            << p.angle << "\n";
    }

    out.close();

    auto [a, l, pr, q] = details(best_global, wh_area);

    cout << "BEST " << case_dir << "\n";
    cout << "bays=" << best_global.size()
         << " area=" << a
         << " loads=" << l
         << " price=" << pr
         << " Q=" << q << "\n";

    print_operator_stats(case_dir, total_stats);

    cout << "Written: " << out_path << "\n";
    cout << "[time] " << case_dir << " elapsed=" << seconds_since(case_start) << "s\n";
}

int main(int argc, char* argv[]) {
    auto total_start = Clock::now();

    if (argc >= 2) {
        solve_case(argv[1]);
    } else {
        for (auto& c : CASES) {
            ifstream f(c + "/warehouse.csv");

            if (f.good()) {
                solve_case(c);
            } else {
                cout << "Skipping " << c << "\n";
            }
        }
    }

    cout << "\n[time] total elapsed=" << seconds_since(total_start) << "s\n";

    return 0;
}
