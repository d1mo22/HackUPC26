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
#include <cstdlib>

using namespace std;
using Clock = chrono::steady_clock;

//const vector<string> CASES = {"Case0", "Case1", "Case2", "Case3","CaseWeird","Case40"};
//const vector<string> CASES = {"CaseAngledA", "CaseAngledB", "CaseAngledC", "CaseAngledD"};
//const vector<string> CASES = {"CaseDiagArmA", "CaseDiagArmB", "CaseDiagArmC", "CaseDiagArmD"};
const vector<string> CASES = {"CaseWeird", "Case1", "Case2", "Case3", "Case0", "Case40", "CaseAngledA", "CaseAngledB", "CaseAngledC", "CaseAngledD", "CaseDiagArmA", "CaseDiagArmB", "CaseDiagArmC", "CaseDiagArmD","CaseForcedAngle"};  // Ordenados por calidad visual (según mi criterio)

const int ITERATIONS = 450;
const int INITIAL_ADDS = 80;
const int RESTARTS = 10;
const int MAX_POINTS_ADD = 80;
const int ANGLE_SAMPLE = 14;
const int NUM_OPERATORS = 8;
const double DIAGONAL_COMPACT_MAX_SHIFT = 12000.0;
const double DIAGONAL_COMPACT_MIN_SHIFT = 25.0;
const int DIAGONAL_COMPACT_MAX_BAYS = 12;
const int DIAGONAL_COMPACT_BINARY_ITERS = 18;
const int SHARED_GAP_MAX_ANCHORS = 24;
const int MAX_TYPE_POINTS = 70;
const int GREEDY_MAX_STEPS = 90;
const double GAP_EXTRA_WEIGHT = 0.65;
const double SPREAD_WEIGHT = 0.08;

const double EPS = 1e-7;
const double PI = acos(-1.0);

thread_local mt19937 rng(42);

array<double, NUM_OPERATORS> OP_WEIGHTS = {
    0.0,
    0.0,
    0.0,
    1.0,
    1.0,
    1.0,
    1.0,
    0.1
};

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
    long long tried[NUM_OPERATORS] = {};
    long long improved[NUM_OPERATORS] = {};
};

const vector<string> OP_NAMES = {
    "add_bay",
    "replace_bay",
    "fill_aggressive",
    "remove_k_and_refill",
    "shared_gap_refill",
    "upgrade_bay",
    "rotate_compact_and_add",
    "add_45_degree_bay"
};

vector<int> ANGLES = {
    0, 45, 90, 135, 180, 225, 270, 315
};

bool is_orthogonal_angle(int angle) {
    int normalized = angle % 360;
    if (normalized < 0) normalized += 360;

    return normalized == 0 || normalized == 90 || normalized == 180 || normalized == 270;
}

bool is_diagonal_45_angle(int angle) {
    int normalized = angle % 360;
    if (normalized < 0) normalized += 360;

    return normalized % 45 == 0 && !is_orthogonal_angle(normalized);
}

bool is_allowed_angle(int angle) {
    int normalized = angle % 360;
    if (normalized < 0) normalized += 360;

    return normalized % 45 == 0;
}

vector<int> prioritized_angles(const vector<int>& angles_source) {
    vector<int> orthogonal;
    vector<int> fallback;

    for (int angle : angles_source) {
        if (!is_allowed_angle(angle)) continue;

        if (is_orthogonal_angle(angle)) {
            orthogonal.push_back(angle);
        } else {
            fallback.push_back(angle);
        }
    }

    shuffle(orthogonal.begin(), orthogonal.end(), rng);
    shuffle(fallback.begin(), fallback.end(), rng);

    orthogonal.insert(orthogonal.end(), fallback.begin(), fallback.end());

    return orthogonal;
}

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

double signed_polygon_area(const vector<Point>& p) {
    double a = 0;

    for (int i = 0; i < (int)p.size(); i++) {
        Point p1 = p[i];
        Point p2 = p[(i + 1) % p.size()];
        a += p1.x * p2.y - p2.x * p1.y;
    }

    return a / 2.0;
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

Point line_intersection(Point a, Point b, Point c, Point d) {
    Point r = {b.x - a.x, b.y - a.y};
    Point s = {d.x - c.x, d.y - c.y};
    double den = r.x * s.y - r.y * s.x;

    if (fabs(den) < EPS) return b;

    double t = ((c.x - a.x) * s.y - (c.y - a.y) * s.x) / den;
    return {a.x + t * r.x, a.y + t * r.y};
}

vector<Point> convex_clip_polygon(const vector<Point>& subject, const vector<Point>& clip) {
    if (subject.empty() || clip.empty()) return {};

    vector<Point> output = subject;
    double orient_sign = signed_polygon_area(clip) >= 0.0 ? 1.0 : -1.0;

    for (int i = 0; i < (int)clip.size(); i++) {
        Point a = clip[i];
        Point b = clip[(i + 1) % clip.size()];
        vector<Point> input = output;
        output.clear();

        if (input.empty()) break;

        auto inside = [&](Point p) {
            return orient_sign * cross(a, b, p) >= -EPS;
        };

        Point prev = input.back();
        bool prev_inside = inside(prev);

        for (Point cur : input) {
            bool cur_inside = inside(cur);

            if (cur_inside != prev_inside) {
                output.push_back(line_intersection(prev, cur, a, b));
            }

            if (cur_inside) {
                output.push_back(cur);
            }

            prev = cur;
            prev_inside = cur_inside;
        }
    }

    return output;
}

double convex_intersection_area(const vector<Point>& a, const vector<Point>& b) {
    if (a.empty() || b.empty()) return 0.0;
    if (!polygons_overlap_area(a, b)) return 0.0;

    vector<Point> clipped = convex_clip_polygon(a, b);

    if (clipped.size() < 3) return 0.0;

    return polygon_area(clipped);
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
    int mode,
    const vector<int>& angles_source = ANGLES
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
            vector<int> angles = prioritized_angles(angles_source);

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
        vector<int> angles = prioritized_angles(ANGLES);

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

Point angle_width_axis(int angle) {
    const Trig& trig = trig_for_angle(angle);
    return {trig.cos_v, trig.sin_v};
}

Point angle_depth_axis(int angle) {
    const Trig& trig = trig_for_angle(angle);
    return {-trig.sin_v, trig.cos_v};
}

bool valid_placed_bay_at(
    const PlacedBay& bay,
    const vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    int ignore
) {
    return valid_candidate(
        bay.x,
        bay.y,
        bay.w,
        bay.d,
        bay.h,
        bay.gap,
        bay.angle,
        sol,
        warehouse,
        obstacles,
        ceiling,
        ignore
    );
}

bool valid_slide_distance(
    const vector<PlacedBay>& sol,
    int idx,
    Point dir,
    double dist,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling
) {
    PlacedBay moved = sol[idx];
    moved.x += dir.x * dist;
    moved.y += dir.y * dist;

    return valid_placed_bay_at(moved, sol, warehouse, obstacles, ceiling, idx);
}

double max_touching_slide_distance(
    const vector<PlacedBay>& sol,
    int idx,
    Point dir,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling
) {
    double lo = 0.0;
    double hi = DIAGONAL_COMPACT_MIN_SHIFT;

    while (
        hi < DIAGONAL_COMPACT_MAX_SHIFT &&
        valid_slide_distance(sol, idx, dir, hi, warehouse, obstacles, ceiling)
    ) {
        lo = hi;
        hi *= 2.0;
    }

    if (hi >= DIAGONAL_COMPACT_MAX_SHIFT &&
        valid_slide_distance(sol, idx, dir, DIAGONAL_COMPACT_MAX_SHIFT, warehouse, obstacles, ceiling)) {
        return 0.0;
    }

    hi = min(hi, DIAGONAL_COMPACT_MAX_SHIFT);

    for (int it = 0; it < DIAGONAL_COMPACT_BINARY_ITERS; it++) {
        double mid = (lo + hi) / 2.0;

        if (valid_slide_distance(sol, idx, dir, mid, warehouse, obstacles, ceiling)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    if (lo < DIAGONAL_COMPACT_MIN_SHIFT) {
        return 0.0;
    }

    return lo;
}

bool compact_diagonal_bays_on_axes(
    vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling
) {
    if (sol.empty()) return false;

    bool changed = false;
    vector<int> order(sol.size());
    iota(order.begin(), order.end(), 0);
    shuffle(order.begin(), order.end(), rng);

    int processed = 0;

    for (int idx : order) {
        if (!is_diagonal_45_angle(sol[idx].angle)) continue;
        if (processed >= DIAGONAL_COMPACT_MAX_BAYS) break;
        processed++;

        Point u = angle_width_axis(sol[idx].angle);
        Point v = angle_depth_axis(sol[idx].angle);

        vector<Point> dirs = {
            u,
            {-u.x, -u.y},
            v,
            {-v.x, -v.y}
        };

        shuffle(dirs.begin(), dirs.end(), rng);

        double best_dist = 0.0;
        Point best_dir = {0, 0};

        for (Point dir : dirs) {
            double dist = max_touching_slide_distance(sol, idx, dir, warehouse, obstacles, ceiling);

            if (dist > best_dist) {
                best_dist = dist;
                best_dir = dir;
            }
        }

        if (best_dist > 0.0) {
            sol[idx].x += best_dir.x * best_dist;
            sol[idx].y += best_dir.y * best_dist;
            changed = true;
        }
    }

    return changed;
}

bool add_shared_gap_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
) {
    if (sol.empty()) return false;

    vector<int> anchors(sol.size());
    iota(anchors.begin(), anchors.end(), 0);
    shuffle(anchors.begin(), anchors.end(), rng);

    if ((int)anchors.size() > SHARED_GAP_MAX_ANCHORS) {
        anchors.resize(SHARED_GAP_MAX_ANCHORS);
    }

    vector<BayType> sorted_types = types;

    sort(sorted_types.begin(), sorted_types.end(), [&](const BayType& a, const BayType& b) {
        return bay_score_mode(a, mode, wh_area) > bay_score_mode(b, mode, wh_area);
    });

    PlacedBay best;
    bool found = false;
    double best_q = 1e100;

    for (int idx : anchors) {
        PlacedBay anchor = sol[idx];

        if (anchor.gap <= 0) continue;

        Point u = angle_width_axis(anchor.angle);
        Point v = angle_depth_axis(anchor.angle);

        for (auto& t : sorted_types) {
            if (t.gap <= 0) continue;

            int angle = (anchor.angle + 180) % 360;
            double depth_offset = anchor.d + anchor.gap + t.d;

            vector<double> lateral_offsets = {
                (double)t.w,
                (double)anchor.w,
                ((double)anchor.w + t.w) / 2.0
            };

            shuffle(lateral_offsets.begin(), lateral_offsets.end(), rng);

            for (double lateral_offset : lateral_offsets) {
                double x = anchor.x + lateral_offset * u.x + depth_offset * v.x;
                double y = anchor.y + lateral_offset * u.y + depth_offset * v.y;

                if (!valid_candidate(
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
                    continue;
                }

                PlacedBay candidate = make_candidate(t, x, y, angle);

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

    if (!found) return false;

    sol.push_back(best);
    return true;
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

    static const vector<int> shared_angles = {
        0, 180,
        90, 270,
        45, 225,
        135, 315
    };

    for (int i = 0; i < k + 5; i++) {
        if (!add_shared_gap_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode)) {
            add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode, shared_angles);
        }
    }

    if (quality(sol, wh_area) > quality(backup, wh_area)) {
        sol = backup;
    }
}

void rotate_compact_and_add(
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
    double backup_q = quality(backup, wh_area);

    vector<int> order(sol.size());
    iota(order.begin(), order.end(), 0);
    shuffle(order.begin(), order.end(), rng);

    int attempts = min(4, (int)order.size());
    vector<PlacedBay> best_sol = sol;
    double best_q = backup_q;
    bool found = false;

    for (int oi = 0; oi < attempts; oi++) {
        int idx = order[oi];
        PlacedBay old = backup[idx];

        vector<PlacedBay> base = backup;
        base.erase(base.begin() + idx);

        vector<int> angles = prioritized_angles(ANGLES);
        int limit = angles.size();

        for (int ai = 0; ai < limit; ai++) {
            int angle = angles[ai];
            if (angle == old.angle) continue;

            if (!valid_candidate(
                    old.x,
                    old.y,
                    old.w,
                    old.d,
                    old.h,
                    old.gap,
                    angle,
                    base,
                    warehouse,
                    obstacles,
                    ceiling
                )) {
                continue;
            }

            vector<PlacedBay> candidate = base;
            PlacedBay rotated = old;
            rotated.angle = angle;
            candidate.push_back(rotated);

            bool added = add_shared_gap_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
            if (!added) {
                add_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
            }

            double q = quality(candidate, wh_area);

            if (q < best_q) {
                best_q = q;
                best_sol = candidate;
                found = true;
            }
        }
    }

    if (found) {
        sol = best_sol;
    }
}

void add_45_degree_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode
) {
    vector<PlacedBay> backup = sol;
    double backup_q = quality(backup, wh_area);

    static const vector<int> diagonal_angles = {45, 135, 225, 315};

    compact_diagonal_bays_on_axes(sol, warehouse, obstacles, ceiling);

    if (!add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode, diagonal_angles)) {
        sol = backup;
        return;
    }

    compact_diagonal_bays_on_axes(sol, warehouse, obstacles, ceiling);

    if (quality(sol, wh_area) > backup_q) {
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

vector<Point> greedy_points(
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<PlacedBay>& sol
) {
    vector<Point> pts = candidate_points(warehouse, obstacles, sol);

    for (auto& p : sol) {
        Point u = angle_width_axis(p.angle);
        Point v = angle_depth_axis(p.angle);

        vector<pair<double, double>> offsets = {
            {p.w, 0},
            {-p.w, 0},
            {0, p.d + p.gap},
            {0, -(p.d + p.gap)},
            {p.w, p.d + p.gap},
            {-p.w, p.d + p.gap},
            {p.w, -(p.d + p.gap)},
            {-p.w, -(p.d + p.gap)}
        };

        for (auto [du, dv] : offsets) {
            pts.push_back({
                round(p.x + du * u.x + dv * v.x),
                round(p.y + du * u.y + dv * v.y)
            });
        }
    }

    sort(pts.begin(), pts.end(), [](const Point& a, const Point& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });

    pts.erase(unique(pts.begin(), pts.end(), [](const Point& a, const Point& b) {
        return fabs(a.x - b.x) < EPS && fabs(a.y - b.y) < EPS;
    }), pts.end());

    return pts;
}

vector<Point> anchor_points(
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<PlacedBay>& sol
) {
    vector<Point> anchors;

    for (auto p : warehouse) anchors.push_back(p);

    for (auto& o : obstacles) {
        anchors.push_back({o.x, o.y});
        anchors.push_back({o.x + o.w, o.y});
        anchors.push_back({o.x, o.y + o.d});
        anchors.push_back({o.x + o.w, o.y + o.d});
    }

    for (auto& p : sol) {
        auto bp = bay_poly(p);
        auto gp = gap_poly(p);

        for (auto q : bp) anchors.push_back(q);
        for (auto q : gp) anchors.push_back(q);
    }

    return anchors;
}

vector<Point> placement_points_for_type(
    const BayType& t,
    int angle,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<PlacedBay>& sol
) {
    vector<Point> pts = greedy_points(warehouse, obstacles, sol);
    vector<Point> anchors = anchor_points(warehouse, obstacles, sol);
    vector<Point> local = make_rotated_rect(0, 0, t.w, t.d, angle);
    vector<Point> local_gap = gap_poly_values(0, 0, t.w, t.d, t.gap, angle);

    local.insert(local.end(), local_gap.begin(), local_gap.end());

    for (Point anchor : anchors) {
        for (Point corner : local) {
            pts.push_back({
                round(anchor.x - corner.x),
                round(anchor.y - corner.y)
            });
        }
    }

    sort(pts.begin(), pts.end(), [](const Point& a, const Point& b) {
        if (fabs(a.x - b.x) > EPS) return a.x < b.x;
        return a.y < b.y;
    });

    pts.erase(unique(pts.begin(), pts.end(), [](const Point& a, const Point& b) {
        return fabs(a.x - b.x) < EPS && fabs(a.y - b.y) < EPS;
    }), pts.end());

    if ((int)pts.size() > MAX_TYPE_POINTS) {
        shuffle(pts.begin(), pts.end(), rng);
        pts.resize(MAX_TYPE_POINTS);
    }

    return pts;
}

double extra_gap_area(const PlacedBay& candidate, const vector<PlacedBay>& sol) {
    vector<Point> gp = gap_poly(candidate);
    if (gp.empty()) return 0.0;

    double extra = polygon_area(gp);

    for (auto& p : sol) {
        vector<Point> other = gap_poly(p);
        if (other.empty()) continue;

        extra -= convex_intersection_area(gp, other);
    }

    return max(0.0, extra);
}

double spread_penalty_after_add(const vector<PlacedBay>& sol, const PlacedBay& candidate, double wh_area) {
    vector<Point> all;

    for (auto& p : sol) {
        auto bp = bay_poly(p);
        all.insert(all.end(), bp.begin(), bp.end());
    }

    auto cp = bay_poly(candidate);
    all.insert(all.end(), cp.begin(), cp.end());

    if (all.empty()) return 0.0;

    Bounds b = polygon_bounds(all);
    double box_area = max(0.0, b.max_x - b.min_x) * max(0.0, b.max_y - b.min_y);

    return box_area / max(1.0, wh_area);
}

double greedy_candidate_score(
    vector<PlacedBay>& sol,
    const PlacedBay& candidate,
    double wh_area
) {
    sol.push_back(candidate);
    double q = quality(sol, wh_area);
    sol.pop_back();

    double gap_extra_ratio = extra_gap_area(candidate, sol) / max(1.0, wh_area);
    double spread = spread_penalty_after_add(sol, candidate, wh_area);

    return q * (1.0 + GAP_EXTRA_WEIGHT * gap_extra_ratio + SPREAD_WEIGHT * spread);
}

bool greedy_add_best(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area
) {
    vector<BayType> sorted_types = types;

    sort(sorted_types.begin(), sorted_types.end(), [&](const BayType& a, const BayType& b) {
        double qa = quality(vector<PlacedBay>{make_candidate(a, 0, 0, 0)}, wh_area);
        double qb = quality(vector<PlacedBay>{make_candidate(b, 0, 0, 0)}, wh_area);
        if (qa != qb) return qa < qb;

        double area_a = (double)a.w * a.d;
        double area_b = (double)b.w * b.d;
        double eff_a = area_a / max(1.0, (double)a.w * (a.d + a.gap));
        double eff_b = area_b / max(1.0, (double)b.w * (b.d + b.gap));

        return eff_a > eff_b;
    });

    PlacedBay best;
    bool found = false;
    double base_q = quality(sol, wh_area);
    double best_q = base_q;
    double best_score = 1e100;

    if (sol.empty()) {
        best_q = 1e100;
    }

    vector<int> angles = prioritized_angles(ANGLES);

    for (auto& t : sorted_types) {
        for (int angle : angles) {
            vector<Point> pts = placement_points_for_type(t, angle, warehouse, obstacles, sol);

            for (auto& p : pts) {
                if (!valid_candidate(
                        p.x,
                        p.y,
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
                    continue;
                }

                PlacedBay candidate = make_candidate(t, p.x, p.y, angle);
                double score = greedy_candidate_score(sol, candidate, wh_area);
                sol.push_back(candidate);
                double q = quality(sol, wh_area);
                sol.pop_back();

                bool improves_q = sol.empty() || q < base_q - EPS;

                if (improves_q && score < best_score) {
                    best_q = q;
                    best_score = score;
                    best = candidate;
                    found = true;
                }
            }
        }
    }

    if (!found) {
        return false;
    }

    sol.push_back(best);
    return true;
}

vector<PlacedBay> greedy_construct(
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area
) {
    vector<PlacedBay> sol;

    for (int step = 0; step < GREEDY_MAX_STEPS; step++) {
        if (!greedy_add_best(sol, types, warehouse, obstacles, ceiling, wh_area)) {
            break;
        }
    }

    return sol;
}

double solve_case(const string& case_dir) {
    auto case_start = Clock::now();

    cout << "\n=== Pipeline solving " << case_dir << " ===\n";

    auto warehouse = read_warehouse(case_dir + "/warehouse.csv");
    auto obstacles = read_obstacles(case_dir + "/obstacles.csv");
    auto ceiling = read_ceiling(case_dir + "/ceiling.csv");
    auto types = read_bays(case_dir + "/types_of_bays.csv");

    double total_wh_area = polygon_area(warehouse);
    double obs_area = obstacles_area(obstacles);
    double wh_area = available_warehouse_area(warehouse, obstacles);

    cout << "area_total=" << total_wh_area
         << " obstacles_area=" << obs_area
         << " available_area=" << wh_area
         << "\n";

    rng.seed(4242 + (int)case_dir.size() * 97);
    vector<PlacedBay> best_global = greedy_construct(types, warehouse, obstacles, ceiling, wh_area);

    bool compact_changed = true;

    for (int pass = 0; pass < 2 && compact_changed; pass++) {
        compact_changed = compact_diagonal_bays_on_axes(best_global, warehouse, obstacles, ceiling);
        while (greedy_add_best(best_global, types, warehouse, obstacles, ceiling, wh_area)) {}
    }

    bool valid = is_valid_solution(best_global, warehouse, obstacles, ceiling);

    if (!valid) {
        best_global.clear();
    }

    string out_path = case_dir + "/solution_pipeline.csv";
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

    cout << "PIPELINE " << case_dir << "\n";
    cout << "bays=" << best_global.size()
         << " area=" << a
         << " loads=" << l
         << " price=" << pr
         << " Q=" << q
         << " valid=" << valid
         << "\n";

    cout << "Written: " << out_path << "\n";
    cout << "[time] " << case_dir << " elapsed=" << seconds_since(case_start) << "s\n";

    return q;
}

int main(int argc, char** argv) {
    auto total_start = Clock::now();
    double sum_q = 0.0;
    int solved_cases = 0;
    vector<string> cases_to_run = CASES;

    if (argc > 1) {
        cases_to_run.clear();

        for (int i = 1; i < argc; i++) {
            cases_to_run.push_back(argv[i]);
        }
    }

    for (auto& c : cases_to_run) {
        ifstream f(c + "/warehouse.csv");

        if (f.good()) {
            double q = solve_case(c);
            sum_q += q;
            solved_cases++;
        } else {
            cout << "Skipping " << c << "\n";
        }
    }

    double objective_q = solved_cases > 0 ? sum_q / solved_cases : 1e100;

    cout << "\nOBJECTIVE_Q " << objective_q << "\n";
    cout << "\n[time] total elapsed=" << seconds_since(total_start) << "s\n";

    return 0;
}
