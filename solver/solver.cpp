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
#include <climits>
#include <filesystem>
#include <functional>

using namespace std;
using Clock = chrono::steady_clock;
namespace fs = std::filesystem;

//const vector<string> CASES = {"Case0", "Case1", "Case2", "Case3","CaseWeird","Case40"};
//const vector<string> CASES = {"CaseAngledA", "CaseAngledB", "CaseAngledC", "CaseAngledD"};
//const vector<string> CASES = {"CaseDiagArmA", "CaseDiagArmB", "CaseDiagArmC", "CaseDiagArmD"};
// Tuning subset: 5 representative cases, ~110s wall time. Picked to span
// axis-aligned dense, axis-aligned sparse-fast, large angled, diag-arm,
// and the forced-angle outlier — covers the dimensions where regressions
// have historically shown up. Use the full 15-case list for final eval.
//const vector<string> CASES = {"Case0", "CaseWeird", "CaseAngledD", "CaseDiagArmD", "CaseForcedAngle"};
const vector<string> PREFERRED_CASE_ORDER = {"CaseWeird", "Case1", "Case2", "Case3", "Case0", "Case40", "CaseAngledA", "CaseAngledB", "CaseAngledC", "CaseAngledD", "CaseDiagArmA", "CaseDiagArmB", "CaseDiagArmC", "CaseDiagArmD","CaseForcedAngle"};

const int ITERATIONS = 450;          // legacy ceiling — actual stop is the deadline
const double CASE_BUDGET_SECONDS = 25.0;  // wall budget per restart; restarts run in parallel
const int INITIAL_ADDS = 80;
const int RESTARTS = 10;
const int MAX_POINTS_ADD = 80;
const int ANGLE_SAMPLE = 14;
const int NUM_OPERATORS = 10;
const double DIAGONAL_COMPACT_MAX_SHIFT = 12000.0;
const double DIAGONAL_COMPACT_MIN_SHIFT = 25.0;
const int DIAGONAL_COMPACT_MAX_BAYS = 12;
const int DIAGONAL_COMPACT_BINARY_ITERS = 18;
const int SHARED_GAP_MAX_ANCHORS = 24;

const double EPS = 1e-7;
const double PI = acos(-1.0);

thread_local mt19937 rng(42);

bool is_test_case_dir(const fs::path& dir) {
    if (!fs::is_directory(dir)) {
        return false;
    }

    static const vector<string> required_files = {
        "warehouse.csv",
        "obstacles.csv",
        "ceiling.csv",
        "types_of_bays.csv"
    };

    for (const string& file : required_files) {
        if (!fs::is_regular_file(dir / file)) {
            return false;
        }
    }

    return true;
}

vector<string> discover_test_cases() {
    vector<string> found;

    for (const auto& entry : fs::directory_iterator(fs::current_path())) {
        if (is_test_case_dir(entry.path())) {
            found.push_back(entry.path().filename().string());
        }
    }

    sort(found.begin(), found.end());

    vector<string> ordered;

    for (const string& preferred : PREFERRED_CASE_ORDER) {
        auto it = find(found.begin(), found.end(), preferred);

        if (it != found.end()) {
            ordered.push_back(preferred);
            found.erase(it);
        }
    }

    ordered.insert(ordered.end(), found.begin(), found.end());
    return ordered;
}

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
    "add_45_degree_bay",
    "position_perturb_and_add",
    "split_bay"
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

    // Ceiling pre-check hoisted before obstacle/collision tests: a tall bay in
    // a low-ceiling region is rejected here in O(segments) instead of after
    // the full O(obstacles + sol) scan. The bay-x span is computed from the
    // already-built candidate polygon bb (cand_bb) which equals minmax_x.
    {
        double min_h = min_ceiling_between(cand_bb.min_x, cand_bb.max_x, ceiling);
        if (h > min_h + EPS) return false;
    }

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
        // edge midpoints — tangent placement against obstacle sides
        pts.push_back({o.x + o.w / 2.0, o.y});
        pts.push_back({o.x + o.w / 2.0, o.y + o.d});
        pts.push_back({o.x, o.y + o.d / 2.0});
        pts.push_back({o.x + o.w, o.y + o.d / 2.0});
    }

    for (auto& p : sol) {
        auto& bp = bay_poly(p);
        auto& gp = gap_poly(p);

        for (auto q : bp) pts.push_back({round(q.x), round(q.y)});
        for (auto q : gp) pts.push_back({round(q.x), round(q.y)});

        // edge midpoints of the bay rectangle — tangent slot finder
        if ((int)bp.size() == 4) {
            for (int i = 0; i < 4; i++) {
                Point a = bp[i];
                Point b = bp[(i + 1) % 4];
                pts.push_back({round((a.x + b.x) / 2.0), round((a.y + b.y) / 2.0)});
            }
        }
    }

    shuffle(pts.begin(), pts.end(), rng);

    if ((int)pts.size() > 500) pts.resize(500);

    return pts;
}

PlacedBay make_candidate(const BayType& t, double x, double y, int angle) {
    PlacedBay p;
    p.id = t.id;
    p.x = x;
    p.y = y;
    p.w = t.w;
    p.d = t.d;
    p.h = t.h;
    p.gap = t.gap;
    p.angle = angle;
    p.price = t.price;
    p.loads = t.loads;
    refresh_cache(p);
    return p;
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

    SpatialIndex idx;
    idx.build(sol);

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
                        ceiling,
                        -1,
                        &idx
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

    QSums cur = compute_sums(sol);
    double best_q = quality_with_added(cur, old.price, old.loads, (double)old.w * old.d, wh_area);
    PlacedBay best = old;
    bool found = false;

    // Build spatial index once over the (already-erased) sol — every type/angle
    // combination tested below queries against the same set, so the build cost
    // is amortised across N_types * ANGLE_SAMPLE valid_candidate calls.
    SpatialIndex idx_grid;
    idx_grid.build(sol);

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
                    ceiling,
                    -1,
                    &idx_grid
                )) {
                double q = quality_with_added(cur, t.price, t.loads, (double)t.w * t.d, wh_area);

                if (q < best_q) {
                    best_q = q;
                    best = make_candidate(t, old.x, old.y, angle);
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

// split_bay: removes one bay and tries to fill its (axis-aligned) bbox with
// multiple smaller bays. The operator wins primarily by increasing total bay
// area: Q = (Σp/Σl)^(2 - area_ratio) is monotonically improved by adding area
// when the base is > 1 (typical). For axis-aligned victims, the bbox equals the
// bay footprint, so we can rarely fit more area inside — operator is mostly
// dead. For *rotated* victims (angle not 0/90/180/270), the bbox is larger
// than w×d, leaving corner pockets that smaller axis-aligned bays may fill.
// Therefore the operator is gated to rotated victims only.
void split_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int /*mode*/
) {
    if (sol.empty()) return;

    vector<PlacedBay> backup = sol;
    double backup_q = quality(backup, wh_area);

    // Find rotated bays (angle not aligned to 0/90/180/270). If none, return.
    vector<int> rotated;
    rotated.reserve(sol.size());
    for (int i = 0; i < (int)sol.size(); i++) {
        int a = sol[i].angle % 360;
        if (a < 0) a += 360;
        if (a != 0 && a != 90 && a != 180 && a != 270) {
            rotated.push_back(i);
        }
    }
    if (rotated.empty()) return;

    int idx = rotated[rng() % rotated.size()];
    const PlacedBay& victim = sol[idx];
    double v_bay_area = (double)victim.w * (double)victim.d;
    double v_bbox_area = (victim.bay_max_x - victim.bay_min_x) *
                         (victim.bay_max_y - victim.bay_min_y);

    // Need meaningful pocket space (bbox materially larger than bay footprint).
    if (v_bbox_area < v_bay_area * 1.15) return;

    // Smaller types — anything that could plausibly fit a corner pocket.
    vector<int> smaller;
    smaller.reserve(types.size());
    for (int ti = 0; ti < (int)types.size(); ti++) {
        double a = (double)types[ti].w * (double)types[ti].d;
        if (a < v_bay_area + EPS) smaller.push_back(ti);
    }
    if (smaller.empty()) return;

    double bx_min = victim.bay_min_x;
    double by_min = victim.bay_min_y;
    double bx_max = victim.bay_max_x;
    double by_max = victim.bay_max_y;

    sol.erase(sol.begin() + idx);

    static const int try_angles[4] = {0, 90, 180, 270};
    int placed = 0;
    const int MAX_ATTEMPTS = 12;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        double rx = uniform_real_distribution<double>(bx_min, bx_max)(rng);
        double ry = uniform_real_distribution<double>(by_min, by_max)(rng);

        int ti = smaller[rng() % smaller.size()];
        const BayType& t = types[ti];

        bool added = false;
        for (int ai = 0; ai < 4 && !added; ai++) {
            int angle = try_angles[ai];
            if (valid_candidate(rx, ry, t.w, t.d, t.h, t.gap, angle,
                                sol, warehouse, obstacles, ceiling)) {
                sol.push_back(make_candidate(t, rx, ry, angle));
                placed++;
                added = true;
            }
        }
    }

    // Reject if ≤1 placed (then it's just replace_bay) or Q didn't improve.
    if (placed < 2 || quality(sol, wh_area) >= backup_q) {
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
            refresh_cache(sol[idx]);
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

    QSums cur = compute_sums(sol);

    // Build spatial index once over sol — anchor loop tries up to
    // SHARED_GAP_MAX_ANCHORS * N_types * 3 valid_candidate calls against the
    // unchanged sol, so amortising the build is a clear win.
    SpatialIndex idx_grid;
    idx_grid.build(sol);

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
                        ceiling,
                        -1,
                        &idx_grid
                    )) {
                    continue;
                }

                double q = quality_with_added(cur, t.price, t.loads, (double)t.w * t.d, wh_area);

                if (q < best_q) {
                    best_q = q;
                    best = make_candidate(t, x, y, angle);
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

    // Cluster removal: 25% chance to remove the k bays nearest to a randomly
    // chosen seed (contiguous spatial void → larger refill opportunities), else
    // fall back to scattered random removal (broader exploration). Empirically
    // 50% over-rotated towards cluster mode and hurt angled cases; 25% gives
    // cluster a meaningful presence without dominating the operator's behavior.
    bool cluster_mode = (rng() % 4 == 0) && (int)sol.size() > k;

    if (cluster_mode) {
        int seed_idx = rng() % sol.size();
        // Center of seed bay
        double sx = (sol[seed_idx].bay_min_x + sol[seed_idx].bay_max_x) * 0.5;
        double sy = (sol[seed_idx].bay_min_y + sol[seed_idx].bay_max_y) * 0.5;

        vector<pair<double, int>> dist_idx;
        dist_idx.reserve(sol.size());
        for (int i = 0; i < (int)sol.size(); i++) {
            double cx = (sol[i].bay_min_x + sol[i].bay_max_x) * 0.5;
            double cy = (sol[i].bay_min_y + sol[i].bay_max_y) * 0.5;
            double dx = cx - sx, dy = cy - sy;
            dist_idx.emplace_back(dx * dx + dy * dy, i);
        }
        // Partial sort to find k nearest
        partial_sort(dist_idx.begin(), dist_idx.begin() + k, dist_idx.end());

        // Collect indices to remove, sort descending, erase in-place
        vector<int> remove_idx;
        remove_idx.reserve(k);
        for (int i = 0; i < k; i++) remove_idx.push_back(dist_idx[i].second);
        sort(remove_idx.begin(), remove_idx.end(), greater<int>());
        for (int idx : remove_idx) sol.erase(sol.begin() + idx);
    } else {
        for (int i = 0; i < k && !sol.empty(); i++) {
            int idx = rng() % sol.size();
            sol.erase(sol.begin() + idx);
        }
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
            refresh_cache(rotated);
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

void shelf_pack_into(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode,
    int orientation,   // primary orientation (tried first per type)
    double off_x,
    double off_y
) {
    Bounds wh_bb = polygon_bounds(warehouse);

    vector<BayType> sorted = types;
    sort(sorted.begin(), sorted.end(), [&](const BayType& a, const BayType& b) {
        return bay_score_mode(a, mode, wh_area) > bay_score_mode(b, mode, wh_area);
    });

    const double X_STEP = 100.0;
    const double Y_SKIP = 200.0;
    const int MAX_ROWS = 500;
    const int MAX_PER_ROW = 1000;

    auto try_place = [&](double x, double y, const BayType& t, int o,
                         double& out_ax, double& out_ay, int& out_angle) -> bool {
        double ax = x;
        double ay = (o == 0) ? y : (y + t.w);
        int angle = (o == 0) ? 0 : 270;
        if (valid_candidate(ax, ay, t.w, t.d, t.h, t.gap, angle,
                            sol, warehouse, obstacles, ceiling)) {
            out_ax = ax; out_ay = ay; out_angle = angle;
            return true;
        }
        return false;
    };

    double y = wh_bb.min_y + off_y;
    int rows = 0;

    while (y < wh_bb.max_y && rows < MAX_ROWS) {
        rows++;
        double x = wh_bb.min_x + off_x;
        double max_row_dim = 0.0;
        bool placed_any_in_row = false;
        int safety = 0;

        while (x < wh_bb.max_x && safety++ < MAX_PER_ROW) {
            bool placed = false;
            for (auto& t : sorted) {
                double ax, ay; int angle;
                int placed_orient = -1;
                // Try primary orientation, then fall back to secondary
                if (try_place(x, y, t, orientation, ax, ay, angle)) {
                    placed_orient = orientation;
                } else if (try_place(x, y, t, 1 - orientation, ax, ay, angle)) {
                    placed_orient = 1 - orientation;
                }
                if (placed_orient >= 0) {
                    sol.push_back(make_candidate(t, ax, ay, angle));
                    if (placed_orient == 0) {
                        x += t.w;
                        max_row_dim = max(max_row_dim, (double)t.d + t.gap);
                    } else {
                        x += (double)t.d + t.gap;
                        max_row_dim = max(max_row_dim, (double)t.w);
                    }
                    placed = true;
                    placed_any_in_row = true;
                    break;
                }
            }
            if (!placed) {
                x += X_STEP;
            }
        }

        if (!placed_any_in_row) {
            y += Y_SKIP;
            continue;
        }
        y += max_row_dim;
    }
}

vector<PlacedBay> build_initial(
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode,
    bool axis_aligned,
    int /*strategy*/
) {
    vector<PlacedBay> sol;

    if (axis_aligned) {
        int min_w = INT_MAX;
        for (auto& t : types) min_w = min(min_w, t.w);
        double off = min_w / 2.0;

        struct Variant { int orientation; double off_x; double off_y; };
        vector<Variant> variants = {
            {0, 0.0, 0.0},
            {1, 0.0, 0.0},
            {0, off, 0.0},
            {1, 0.0, off}
        };

        double best_q = 1e100;
        vector<PlacedBay> best;
        for (auto& v : variants) {
            vector<PlacedBay> tmp;
            shelf_pack_into(tmp, types, warehouse, obstacles, ceiling, wh_area, mode,
                            v.orientation, v.off_x, v.off_y);
            double q = quality(tmp, wh_area);
            if (q < best_q) {
                best_q = q;
                best = tmp;
            }
        }
        sol = best;
    }

    for (int i = 0; i < INITIAL_ADDS; i++) {
        if (!add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode)) {
            break;
        }
    }

    return sol;
}

bool warehouse_is_axis_aligned(const vector<Point>& warehouse) {
    for (int i = 0; i < (int)warehouse.size(); i++) {
        Point a = warehouse[i];
        Point b = warehouse[(i + 1) % warehouse.size()];
        double dx = fabs(b.x - a.x);
        double dy = fabs(b.y - a.y);
        if (dx > EPS && dy > EPS) return false;
    }
    return true;
}

// position_perturb_and_add (op 8):
// Pick a random bay, try shifting it by small/medium offsets along x and y axes.
// If the shift is valid, attempt to add a new bay (preferring shared-gap, then add_bay).
// Accept the new state only if Q improves vs the original solution.
//
// Symmetric counterpart to compact_diagonal_bays_on_axes but for orthogonal bays:
// the goal is to nudge a bay slightly to free a pocket large enough for one
// extra placement. Position-only moves don't change Q themselves, so the win
// has to come from the follow-up add_bay.
void position_perturb_and_add(
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

    int attempts = min(2, (int)order.size());

    // Offsets to try, in mm. Tried as ±dx and ±dy for each magnitude.
    // Kept small: large offsets rarely help (the bay was placed where it was
    // for a reason) and each tried offset costs a valid_candidate call.
    static const double OFFS[] = { 100.0, 300.0 };
    static const int N_OFFS = (int)(sizeof(OFFS) / sizeof(OFFS[0]));

    vector<PlacedBay> best_sol = sol;
    double best_q = backup_q;
    bool found = false;

    for (int oi = 0; oi < attempts; oi++) {
        int idx = order[oi];
        const PlacedBay& old = backup[idx];

        // 4 directions × 2 magnitudes = 8 candidate positions per bay.
        for (int k = 0; k < N_OFFS; k++) {
            double mag = OFFS[k];
            double dxy[4][2] = {
                { mag, 0.0 },
                { -mag, 0.0 },
                { 0.0, mag },
                { 0.0, -mag }
            };

            for (int di = 0; di < 4; di++) {
                double new_x = old.x + dxy[di][0];
                double new_y = old.y + dxy[di][1];

                // Validate the moved bay against everything except itself.
                if (!valid_candidate(
                        new_x, new_y,
                        old.w, old.d, old.h, old.gap,
                        old.angle,
                        backup,
                        warehouse,
                        obstacles,
                        ceiling,
                        idx
                    )) {
                    continue;
                }

                // Build candidate: backup with the moved bay.
                vector<PlacedBay> candidate = backup;
                candidate[idx].x = new_x;
                candidate[idx].y = new_y;
                refresh_cache(candidate[idx]);

                // Use the cheap anchor-based add (shared-gap) only.
                // The full add_bay was too expensive and dominated cost.
                if (!add_shared_gap_bay(
                        candidate, types, warehouse, obstacles, ceiling, wh_area, mode
                    )) {
                    continue;  // perturb without follow-up add: Q unchanged, skip
                }

                double q = quality(candidate, wh_area);

                if (q < best_q) {
                    best_q = q;
                    best_sol = candidate;
                    found = true;
                }
            }
        }
    }

    if (found) {
        sol = best_sol;
    }
}

vector<int> build_active_ops(bool axis_aligned) {
    vector<int> ops;
    // op 0 (add_bay): re-enable post-init growth — was missing entirely before
    for (int i = 0; i < 4; i++) ops.push_back(0);
    // op 1 (replace_bay): cheap diversification, position-changing
    for (int i = 0; i < 2; i++) ops.push_back(1);
    // ops 3,4,5: universal heavy hitters, weighted by observed improvement rate
    for (int i = 0; i < 6; i++) ops.push_back(3);
    for (int i = 0; i < 8; i++) ops.push_back(4);
    for (int i = 0; i < 4; i++) ops.push_back(5);
    // ops 6,7: keep available everywhere (some axis-aligned cases still benefit),
    // but with reduced weight when warehouse is axis-aligned
    if (axis_aligned) {
        ops.push_back(6);
        ops.push_back(7);
    } else {
        for (int i = 0; i < 4; i++) ops.push_back(6);
        for (int i = 0; i < 2; i++) ops.push_back(7);
    }
    // op 8 (position_perturb_and_add): orthogonal-bay symmetric of op 6 for
    // axis-aligned cases. Kept low-weight because each call is expensive
    // (8 candidate positions × valid_candidate + add_shared_gap_bay).
    if (axis_aligned) {
        for (int i = 0; i < 2; i++) ops.push_back(8);
    } else {
        ops.push_back(8);
    }
    // op 9 (split_bay): replaces one large bay with multiple smaller ones inside
    // its footprint. Rate is unknown; start at weight 2 (same scale as op 1) and
    // let adaptive sampling tune it. Each call is cheap (one bay removed, up to
    // 12 placement attempts inside one bbox).
    for (int i = 0; i < 2; i++) ops.push_back(9);
    return ops;
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
    bool axis_aligned,
    Clock::time_point deadline
) {
    vector<PlacedBay> best = sol;
    double best_q = quality(best, wh_area);

    const vector<int> active_ops = build_active_ops(axis_aligned);

    // Build the unique-ops set + the static-distribution prior weight per op
    // (so the warmup and prior reflect the hand-tuned mix from build_active_ops).
    vector<int> unique_ops;
    int prior_count[NUM_OPERATORS] = {};
    for (int o : active_ops) {
        prior_count[o]++;
        if (find(unique_ops.begin(), unique_ops.end(), o) == unique_ops.end()) {
            unique_ops.push_back(o);
        }
    }

    // Rolling per-operator counters used by the adaptive sampler.
    // Decayed periodically so dead operators get demoted over time.
    long long roll_tried[NUM_OPERATORS] = {};
    long long roll_improved[NUM_OPERATORS] = {};

    const int WARMUP_ITERS = 150;
    const int DECAY_PERIOD = 300;
    int iter = 0;

    while (Clock::now() < deadline) {

        vector<PlacedBay> candidate = best;

        int op;
        if (iter < WARMUP_ITERS) {
            // Warmup: use the original static distribution so every op has a
            // chance to gather statistics before adaptive sampling kicks in.
            op = active_ops[rng() % active_ops.size()];
        } else {
            // Adaptive sampling. Weight = prior * (improved + 1) / (tried + 4).
            // Multiplying by the prior preserves the hand-tuned axis-aligned
            // gating (op 6/7 weights, etc.) as a soft preference.
            double total = 0.0;
            double w[NUM_OPERATORS] = {};
            for (int o : unique_ops) {
                double r = (double)(roll_improved[o] + 1) / (double)(roll_tried[o] + 4);
                w[o] = (double)prior_count[o] * r;
                total += w[o];
            }

            double pick = uniform_real_distribution<double>(0.0, total)(rng);
            double acc = 0.0;
            op = unique_ops.back();
            for (int o : unique_ops) {
                acc += w[o];
                if (pick <= acc) { op = o; break; }
            }
        }

        stats.tried[op]++;
        roll_tried[op]++;

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
        else if (op == 5) {
            upgrade_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }
        else if (op == 6) {
            rotate_compact_and_add(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }
        else if (op == 7) {
            add_45_degree_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }
        else if (op == 8) {
            position_perturb_and_add(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }
        else {
            split_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }

        double q = quality(candidate, wh_area);

        if (q < best_q) {
            best = candidate;
            best_q = q;
            stats.improved[op]++;
            roll_improved[op]++;
        }

        iter++;
        if (iter % DECAY_PERIOD == 0) {
            // Halve rolling counters so the window favours recent behaviour.
            for (int o : unique_ops) {
                roll_tried[o] /= 2;
                roll_improved[o] /= 2;
            }
        }
    }

    return best;
}

// hill_sa — simulated-annealing variant of hill().
//
// Differences from hill():
//  * Mutations are applied to a "current" state, not always to the best.
//  * Worsening moves accepted with probability exp(-dQ / T).
//  * T cools linearly from T0 (≈5% of starting Q) toward ~0 over the deadline.
//  * `best` is tracked separately and returned, so SA never loses a good state.
//
// Per-op adaptive weights are reused as-is (driven off the same rolling
// counters as hill()), because operator effectiveness should be similar; the
// difference is only the acceptance rule.
vector<PlacedBay> hill_sa(
    vector<PlacedBay> sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode,
    OperatorStats& stats,
    bool axis_aligned,
    Clock::time_point deadline,
    double t0_frac = 0.05
) {
    auto sa_start = Clock::now();
    double total_seconds = chrono::duration<double>(deadline - sa_start).count();
    if (total_seconds <= 0.0) total_seconds = 1.0;

    vector<PlacedBay> current = sol;
    double current_q = quality(current, wh_area);

    vector<PlacedBay> best = current;
    double best_q = current_q;

    // T0 controlled by caller (default 5%). Per-case optimal T0 differs:
    // axis-aligned cases prefer ~3%, angled/forced-angle cases benefit from
    // ~8%. Spreading across restarts hedges this — best-of-restart wins.
    double T0 = max(20.0, t0_frac * current_q);

    const vector<int> active_ops = build_active_ops(axis_aligned);

    vector<int> unique_ops;
    int prior_count[NUM_OPERATORS] = {};
    for (int o : active_ops) {
        prior_count[o]++;
        if (find(unique_ops.begin(), unique_ops.end(), o) == unique_ops.end()) {
            unique_ops.push_back(o);
        }
    }

    long long roll_tried[NUM_OPERATORS] = {};
    long long roll_improved[NUM_OPERATORS] = {};

    const int WARMUP_ITERS = 150;
    const int DECAY_PERIOD = 300;
    int iter = 0;

    while (Clock::now() < deadline) {
        // Mutations are applied to current (SA chain), not to best.
        vector<PlacedBay> candidate = current;

        int op;
        if (iter < WARMUP_ITERS) {
            op = active_ops[rng() % active_ops.size()];
        } else {
            double total = 0.0;
            double w[NUM_OPERATORS] = {};
            for (int o : unique_ops) {
                double r = (double)(roll_improved[o] + 1) / (double)(roll_tried[o] + 4);
                w[o] = (double)prior_count[o] * r;
                total += w[o];
            }
            double pick = uniform_real_distribution<double>(0.0, total)(rng);
            double acc = 0.0;
            op = unique_ops.back();
            for (int o : unique_ops) {
                acc += w[o];
                if (pick <= acc) { op = o; break; }
            }
        }

        stats.tried[op]++;
        roll_tried[op]++;

        if (op == 0) {
            add_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        } else if (op == 1) {
            replace_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        } else if (op == 2) {
            fill_aggressive(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        } else if (op == 3) {
            remove_k_and_refill(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        } else if (op == 4) {
            shared_gap_refill(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        } else if (op == 5) {
            upgrade_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        } else if (op == 6) {
            rotate_compact_and_add(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        } else if (op == 7) {
            add_45_degree_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        } else if (op == 8) {
            position_perturb_and_add(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        } else {
            split_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, mode);
        }

        double q = quality(candidate, wh_area);

        // Linear cooling: progress = elapsed / total, T = T0 * (1 - progress).
        double elapsed = chrono::duration<double>(Clock::now() - sa_start).count();
        double progress = min(1.0, elapsed / total_seconds);
        double T = max(1e-6, T0 * (1.0 - progress));

        bool accept;
        if (q < current_q) {
            accept = true;
        } else {
            double dQ = q - current_q;
            double prob = exp(-dQ / T);
            double u = uniform_real_distribution<double>(0.0, 1.0)(rng);
            accept = (u < prob);
        }

        if (accept) {
            current = candidate;
            current_q = q;
            if (q < best_q) {
                best = candidate;
                best_q = q;
                stats.improved[op]++;
                roll_improved[op]++;
            }
        }

        iter++;
        if (iter % DECAY_PERIOD == 0) {
            for (int o : unique_ops) {
                roll_tried[o] /= 2;
                roll_improved[o] /= 2;
            }
        }
    }

    return best;
}

void print_operator_stats(const string& case_dir, const OperatorStats& stats) {
    cout << "\nOperator stats for " << case_dir << ":\n";

    for (int i = 0; i < NUM_OPERATORS; i++) {
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

    double total_wh_area = polygon_area(warehouse);
    double obs_area = obstacles_area(obstacles);
    double wh_area = available_warehouse_area(warehouse, obstacles);

    bool axis_aligned = warehouse_is_axis_aligned(warehouse);

    cout << "area_total=" << total_wh_area
         << " obstacles_area=" << obs_area
         << " available_area=" << wh_area
         << " axis_aligned=" << axis_aligned
         << "\n";

    auto deadline = Clock::now() + chrono::milliseconds((long long)(CASE_BUDGET_SECONDS * 1000.0));

    auto worker = [&](int r) {
        int mode = r % 4;

        rng.seed(42 + r * 1000 + (int)(hash<string>{}(case_dir) % 100000));

        cout << "Restart " << r + 1 << "/" << RESTARTS
             << " started | mode=" << mode << "\n";

        int strategy = r % 4;
        auto sol = build_initial(types, warehouse, obstacles, ceiling, wh_area, mode, axis_aligned, strategy);

        OperatorStats stats;

        // Mostly SA, with two strict-improvement hill restarts as a safety
        // net. The 8 SA restarts are split across three T0 fractions to hedge
        // per-case sensitivity: 3% (cold) is best on tight axis-aligned cases,
        // 8% (hot) helps angled/forced-angle cases escape larger local minima.
        if (r < 2) {
            sol = hill(sol, types, warehouse, obstacles, ceiling, wh_area, mode, stats, axis_aligned, deadline);
        } else {
            // r in [2..9]: distribute 2x cold (3%), 4x medium (5%), 2x hot (8%).
            double t0_frac;
            if (r <= 3) t0_frac = 0.03;
            else if (r <= 7) t0_frac = 0.05;
            else t0_frac = 0.08;
            sol = hill_sa(sol, types, warehouse, obstacles, ceiling, wh_area, mode, stats, axis_aligned, deadline, t0_frac);
        }

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

        for (int i = 0; i < NUM_OPERATORS; i++) {
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
    vector<string> cases_to_run = discover_test_cases();

    cout << "test_cases_found=" << cases_to_run.size() << "\n";

    for (auto& c : cases_to_run) {
        solve_case(c);
    }

    cout << "\n[time] total elapsed=" << seconds_since(total_start) << "s\n";

    return 0;
}
