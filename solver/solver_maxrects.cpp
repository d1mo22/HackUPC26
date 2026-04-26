// solver_maxrects.cpp — Constructive heuristic following the MaxRects approach
// described in the spec text. Axis-aligned bays only (Rotation 0 or 1, per CSV).
//
// High-level algorithm:
//   1. Read warehouse polygon, obstacles, ceiling, bay types.
//   2. Initialize the free-rectangle list as the polygon's bounding box minus
//      the obstacles (carved by rectangle splitting), then prune contained
//      rectangles. Polygon containment is enforced lazily at placement time.
//   3. Sort bay types by a value-aware score that combines area utility with
//      the price/loads ratio (the Q metric penalizes high price/loads).
//   4. Repeatedly try every (type, rotation) on every free rectangle, scoring
//      candidate placements with Best Short Side Fit (BSSF). After each
//      placement, split intersecting free rects into up to 4 remainders and
//      re-prune contained rects.
//   5. Stop when no candidate fits. Run multiple ordering / fit-strategy modes
//      within a wall-time budget and keep the best Q.
//
// Compile (macOS):
//   g++ -O3 -std=c++17 -o /tmp/solver_maxrects solver/solver_maxrects.cpp
// Run from solver/ so the Case* dirs resolve relatively.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace std;
using Clock = chrono::steady_clock;

// ============================ CONFIG ============================

const vector<string> CASES = {
    "Case0", "Case1", "Case2", "Case3", "CaseWeird"
};

const double CASE_BUDGET_SECONDS = 25.0;
const double EPS = 1e-7;

// Free-rect cap. The spec warns about "exponential inflation"; we prune
// contained rectangles after every split, but keep a hard ceiling as a guard.
const int MAX_FREE_RECTS = 4000;

enum FitStrategy {
    FIT_BSSF = 0,  // Best Short Side Fit (primary per spec)
    FIT_BLSF = 1,  // Best Long Side Fit
    FIT_BAF  = 2,  // Best Area Fit
    FIT_BL   = 3,  // Bottom-Left
    NUM_FIT_STRATEGIES = 4
};

enum OrderMode {
    ORDER_VALUE_DENSITY = 0,  // -(price/loads) per area  — Q-aware
    ORDER_AREA_DESC     = 1,  // FFD by area
    ORDER_LOAD_PRICE    = 2,  // -(price/loads) flat       — knapsack-ish
    ORDER_AREA_VALUE    = 3,  // mixed
    NUM_ORDER_MODES     = 4
};

// ============================ TYPES ============================

struct Point { double x, y; };

struct BayType {
    int id;
    int w, d;
    int h, gap;
    int loads, price;
};

struct Obstacle { double x, y, w, d; };

struct PlacedBay {
    int type_id;
    double x, y;
    int w, d;          // post-rotation footprint
    int rotation;      // 0 or 1
    int loads, price;
};

struct FreeRect {
    double x, y, w, h;
};

struct CandidatePlacement {
    int type_index;
    int rotation;
    double x, y;
    double bay_w, bay_d;       // bay footprint after rotation
    double foot_w, foot_h;     // bay + gap footprint (w & h on the plane)
    double score1, score2;     // primary / tiebreak (lower is better)
    bool valid;
};

// ============================ CSV I/O ============================

static vector<string> split_csv_line(const string& line) {
    vector<string> out;
    string cur;
    for (char c : line) {
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    for (auto& s : out) {
        while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    }
    return out;
}

static bool file_empty_or_missing(const string& path) {
    ifstream f(path);
    if (!f.good()) return true;
    string line;
    while (getline(f, line)) {
        for (char c : line) if (!isspace((unsigned char)c)) return false;
    }
    return true;
}

static bool try_stod(const string& s, double& out) {
    if (s.empty()) return false;
    try { size_t p = 0; out = stod(s, &p); return p > 0; }
    catch (...) { return false; }
}

static bool try_stoi(const string& s, int& out) {
    if (s.empty()) return false;
    try { size_t p = 0; out = stoi(s, &p); return p > 0; }
    catch (...) { return false; }
}

static vector<Point> read_warehouse(const string& path) {
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

static vector<Obstacle> read_obstacles(const string& path) {
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

static vector<pair<double, double>> read_ceiling(const string& path) {
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

static vector<BayType> read_bays(const string& path) {
    vector<BayType> res;
    ifstream f(path);
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() < 7) continue;
        int id, w, d, h, gap, loads, price;
        if (!try_stoi(v[0], id)    || !try_stoi(v[1], w)     ||
            !try_stoi(v[2], d)     || !try_stoi(v[3], h)     ||
            !try_stoi(v[4], gap)   || !try_stoi(v[5], loads) ||
            !try_stoi(v[6], price)) continue;
        res.push_back({id, w, d, h, gap, loads, price});
    }
    return res;
}

// ============================ GEOMETRY ============================

static double polygon_area(const vector<Point>& p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); i++) {
        const Point& p1 = p[i];
        const Point& p2 = p[(i + 1) % p.size()];
        a += p1.x * p2.y - p2.x * p1.y;
    }
    return fabs(a) / 2.0;
}

// Vertical ray-cast, robust against vertices.
static bool point_strictly_inside(double x, double y, const vector<Point>& poly) {
    bool inside = false;
    int n = (int)poly.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        double xi = poly[i].x, yi = poly[i].y;
        double xj = poly[j].x, yj = poly[j].y;
        bool crosses = ((yi > y) != (yj > y));
        if (crosses) {
            double xint = (xj - xi) * (y - yi) / (yj - yi) + xi;
            if (x < xint) inside = !inside;
        }
    }
    return inside;
}

// Sample interior of axis-aligned rectangle to confirm it lies inside poly.
// Uses a small grid of test points; conservative for non-convex polygons.
static bool rect_inside_polygon(double rx, double ry, double rw, double rh,
                                const vector<Point>& poly) {
    // Quick reject: midpoint
    if (!point_strictly_inside(rx + rw / 2.0, ry + rh / 2.0, poly)) return false;

    // Sample edges at fractions to catch concavities cutting through.
    const int N = 5;
    for (int i = 0; i <= N; i++) {
        double fx = (double)i / N;
        for (int j = 0; j <= N; j++) {
            double fy = (double)j / N;
            // Inset by EPS so that vertices on the polygon boundary still test
            // strictly inside (avoids flakiness when rect aligns with polygon edge).
            double tx = rx + (fx == 0 ? EPS : (fx == 1 ? rw - EPS : fx * rw));
            double ty = ry + (fy == 0 ? EPS : (fy == 1 ? rh - EPS : fy * rh));
            if (!point_strictly_inside(tx, ty, poly)) return false;
        }
    }
    return true;
}

static double min_ceiling_between(double x1, double x2,
                                  const vector<pair<double, double>>& ceiling) {
    if (ceiling.empty()) return 1e18;
    if (x1 > x2) swap(x1, x2);
    double ans = 1e18;
    for (size_t i = 0; i < ceiling.size(); i++) {
        double sx1 = ceiling[i].first;
        double h = ceiling[i].second;
        double sx2 = (i + 1 < ceiling.size()) ? ceiling[i + 1].first : 1e18;
        double left = max(x1, sx1);
        double right = min(x2, sx2);
        if (left < right + EPS) ans = min(ans, h);
    }
    return ans;
}

static inline bool rect_intersects(double ax, double ay, double aw, double ah,
                                   double bx, double by, double bw, double bh) {
    return !(ax + aw <= bx + EPS || bx + bw <= ax + EPS ||
             ay + ah <= by + EPS || by + bh <= ay + EPS);
}

// True iff inner is contained in outer (axis-aligned).
static inline bool rect_contains(const FreeRect& outer, const FreeRect& inner) {
    return inner.x >= outer.x - EPS &&
           inner.y >= outer.y - EPS &&
           inner.x + inner.w <= outer.x + outer.w + EPS &&
           inner.y + inner.h <= outer.y + outer.h + EPS;
}

// ============================ MAXRECTS CORE ============================

// Split `f` by removing the axis-aligned `used` rectangle. Up to 4 remainders
// (top, bottom, left, right) appended to `out`. If `used` doesn't intersect
// `f` (positive area), `f` itself is preserved.
static void split_free_rect(const FreeRect& f,
                            double ux, double uy, double uw, double uh,
                            vector<FreeRect>& out) {
    if (!rect_intersects(f.x, f.y, f.w, f.h, ux, uy, uw, uh)) {
        out.push_back(f);
        return;
    }
    // Top remainder
    if (uy + uh < f.y + f.h - EPS) {
        FreeRect r;
        r.x = f.x;
        r.y = uy + uh;
        r.w = f.w;
        r.h = f.y + f.h - (uy + uh);
        if (r.w > EPS && r.h > EPS) out.push_back(r);
    }
    // Bottom remainder
    if (uy > f.y + EPS) {
        FreeRect r;
        r.x = f.x;
        r.y = f.y;
        r.w = f.w;
        r.h = uy - f.y;
        if (r.w > EPS && r.h > EPS) out.push_back(r);
    }
    // Left remainder
    if (ux > f.x + EPS) {
        FreeRect r;
        r.x = f.x;
        r.y = f.y;
        r.w = ux - f.x;
        r.h = f.h;
        if (r.w > EPS && r.h > EPS) out.push_back(r);
    }
    // Right remainder
    if (ux + uw < f.x + f.w - EPS) {
        FreeRect r;
        r.x = ux + uw;
        r.y = f.y;
        r.w = f.x + f.w - (ux + uw);
        r.h = f.h;
        if (r.w > EPS && r.h > EPS) out.push_back(r);
    }
}

// Remove any free rectangle that is fully contained in another. The spec calls
// this out as a critical step to prevent the list from blowing up.
static void prune_contained(vector<FreeRect>& rects) {
    int n = (int)rects.size();
    vector<bool> dead(n, false);
    for (int i = 0; i < n; i++) {
        if (dead[i]) continue;
        for (int j = 0; j < n; j++) {
            if (i == j || dead[j]) continue;
            if (rect_contains(rects[j], rects[i])) {
                dead[i] = true;
                break;
            }
        }
    }
    vector<FreeRect> kept;
    kept.reserve(n);
    for (int i = 0; i < n; i++) if (!dead[i]) kept.push_back(rects[i]);
    rects.swap(kept);
}

// Detect whether the warehouse polygon is rectilinear (every edge axis-aligned).
static bool warehouse_is_rectilinear(const vector<Point>& warehouse) {
    int n = (int)warehouse.size();
    for (int i = 0; i < n; i++) {
        const Point& a = warehouse[i];
        const Point& b = warehouse[(i + 1) % n];
        double dx = fabs(b.x - a.x);
        double dy = fabs(b.y - a.y);
        if (dx > EPS && dy > EPS) return false;
    }
    return true;
}

// Decompose a rectilinear polygon into a union of axis-aligned rectangles by
// horizontal-strip sweep. For each pair of consecutive distinct y-values, find
// the horizontal segments interior to the polygon at that y-band and emit a
// rect for each [x_in, x_out) interval.
static vector<FreeRect> decompose_rectilinear(const vector<Point>& warehouse) {
    vector<FreeRect> out;
    vector<double> ys;
    for (auto& p : warehouse) ys.push_back(p.y);
    sort(ys.begin(), ys.end());
    ys.erase(unique(ys.begin(), ys.end(),
                    [](double a, double b) { return fabs(a - b) < EPS; }),
            ys.end());

    int n = (int)warehouse.size();
    for (size_t i = 0; i + 1 < ys.size(); i++) {
        double y0 = ys[i];
        double y1 = ys[i + 1];
        double ym = (y0 + y1) / 2.0;
        // Collect x-intersections of the horizontal line y=ym with polygon edges.
        vector<double> xs;
        for (int k = 0; k < n; k++) {
            const Point& a = warehouse[k];
            const Point& b = warehouse[(k + 1) % n];
            // Only vertical edges (in a rectilinear polygon) cross horizontal lines.
            if (fabs(a.x - b.x) > EPS) continue;
            double yL = min(a.y, b.y);
            double yH = max(a.y, b.y);
            if (ym > yL + EPS && ym < yH - EPS) {
                xs.push_back(a.x);
            }
        }
        sort(xs.begin(), xs.end());
        // Even-odd: pairs (xs[2k], xs[2k+1]) are inside-spans.
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            double x0 = xs[k], x1 = xs[k + 1];
            if (x1 - x0 > EPS && y1 - y0 > EPS) {
                out.push_back({x0, y0, x1 - x0, y1 - y0});
            }
        }
    }
    return out;
}

// Build the initial free-rect list. For rectilinear polygons we decompose into
// strips so concavities are exact. For non-rectilinear shapes we fall back to
// the bounding-box plus lazy polygon containment per placement.
static vector<FreeRect> initial_free_rects(const vector<Point>& warehouse,
                                           const vector<Obstacle>& obstacles) {
    vector<FreeRect> rects;
    if (warehouse_is_rectilinear(warehouse)) {
        rects = decompose_rectilinear(warehouse);
    } else {
        double minx = warehouse[0].x, maxx = warehouse[0].x;
        double miny = warehouse[0].y, maxy = warehouse[0].y;
        for (auto& p : warehouse) {
            minx = min(minx, p.x); maxx = max(maxx, p.x);
            miny = min(miny, p.y); maxy = max(maxy, p.y);
        }
        rects.push_back({minx, miny, maxx - minx, maxy - miny});
    }

    for (auto& o : obstacles) {
        vector<FreeRect> next;
        next.reserve(rects.size() * 2);
        for (auto& f : rects) split_free_rect(f, o.x, o.y, o.w, o.d, next);
        rects.swap(next);
        prune_contained(rects);
    }
    return rects;
}

// ============================ SCORING & ORDER ============================

static double bay_order_score(const BayType& t, OrderMode mode, double wh_area) {
    double area = (double)t.w * t.d;
    double pl = (double)t.price / max(1.0, (double)t.loads);
    double area_ratio = area / max(1.0, wh_area);
    switch (mode) {
        case ORDER_AREA_DESC:    return area_ratio;
        case ORDER_LOAD_PRICE:   return -pl;
        case ORDER_VALUE_DENSITY: {
            // Higher score = better. We *want* low price/loads (penalty in Q)
            // and high area (denominator boost). Combine as area / pl.
            return area_ratio / max(1.0, pl);
        }
        case ORDER_AREA_VALUE:
        default:
            return 1.5 * area_ratio - 0.8 * pl / 1000.0;
    }
}

// Compute (score1, score2) per fit strategy. Lower is better. score2 is a
// tiebreaker (Bottom-Left preference).
static void score_placement(FitStrategy fit, const FreeRect& f,
                            double bw, double bh,
                            double& s1, double& s2) {
    double leftover_w = f.w - bw;
    double leftover_h = f.h - bh;
    double placed_x = f.x;
    double placed_y = f.y;
    s2 = placed_y * 1e6 + placed_x;  // BL tiebreak
    switch (fit) {
        case FIT_BSSF: {
            double short_side = min(leftover_w, leftover_h);
            double long_side  = max(leftover_w, leftover_h);
            s1 = short_side;
            s2 = long_side;
            break;
        }
        case FIT_BLSF: {
            double short_side = min(leftover_w, leftover_h);
            double long_side  = max(leftover_w, leftover_h);
            s1 = long_side;
            s2 = short_side;
            break;
        }
        case FIT_BAF: {
            double area_diff = f.w * f.h - bw * bh;
            s1 = area_diff;
            break;
        }
        case FIT_BL:
        default: {
            s1 = placed_y + bh;
            s2 = placed_x;
            break;
        }
    }
}

// ============================ PLACEMENT ============================

// Given a candidate placement (footprint=bay+gap), test all extra constraints
// that aren't captured by the free-rect list:
//  - bay rect inside polygon (concavities)
//  - footprint inside polygon
//  - bay height ≤ ceiling above the bay span
static bool placement_passes_constraints(double bx, double by, double bw, double bd,
                                         double fw, double fh, int height,
                                         const vector<Point>& warehouse,
                                         const vector<pair<double, double>>& ceiling) {
    if (!rect_inside_polygon(bx, by, bw, bd, warehouse)) return false;
    if (!rect_inside_polygon(bx, by, fw, fh, warehouse)) return false;
    double cmin = min_ceiling_between(bx, bx + bw, ceiling);
    if (height > cmin + EPS) return false;
    return true;
}

static CandidatePlacement find_best_placement(
    const vector<FreeRect>& rects,
    const vector<BayType>& types,
    const vector<int>& order,
    const vector<Point>& warehouse,
    const vector<pair<double, double>>& ceiling,
    FitStrategy fit
) {
    CandidatePlacement best;
    best.valid = false;
    best.score1 = 1e100;
    best.score2 = 1e100;

    for (int ti : order) {
        const BayType& t = types[ti];
        // Two rotations: 0 (w×d) and 1 (d×w). Gap extends along the depth axis.
        for (int rot = 0; rot < 2; rot++) {
            double bw = (rot == 0) ? t.w : t.d;
            double bd = (rot == 0) ? t.d : t.w;
            // Footprint = bay + gap along depth direction. We model the gap on
            // the +y side; mirror on -y is handled implicitly by the free-rect
            // splits (other bays will already have reserved their own gap).
            double fw = bw;
            double fh = bd + t.gap;

            for (const FreeRect& f : rects) {
                if (fw > f.w + EPS) continue;
                if (fh > f.h + EPS) continue;
                double bx = f.x;
                double by = f.y;
                if (!placement_passes_constraints(bx, by, bw, bd, fw, fh, t.h,
                                                  warehouse, ceiling)) {
                    continue;
                }
                double s1, s2;
                score_placement(fit, f, fw, fh, s1, s2);
                if (s1 < best.score1 - EPS ||
                    (fabs(s1 - best.score1) < EPS && s2 < best.score2 - EPS)) {
                    best.valid = true;
                    best.type_index = ti;
                    best.rotation = rot;
                    best.x = bx;
                    best.y = by;
                    best.bay_w = bw;
                    best.bay_d = bd;
                    best.foot_w = fw;
                    best.foot_h = fh;
                    best.score1 = s1;
                    best.score2 = s2;
                }
            }
        }
    }
    return best;
}

static void apply_placement(vector<FreeRect>& rects, const CandidatePlacement& p) {
    vector<FreeRect> next;
    next.reserve(rects.size() * 2);
    for (auto& f : rects) {
        split_free_rect(f, p.x, p.y, p.foot_w, p.foot_h, next);
    }
    rects.swap(next);
    prune_contained(rects);
    if ((int)rects.size() > MAX_FREE_RECTS) {
        // Hard cap: drop the smallest rects to bound runtime in degenerate cases.
        sort(rects.begin(), rects.end(),
             [](const FreeRect& a, const FreeRect& b) { return a.w * a.h > b.w * b.h; });
        rects.resize(MAX_FREE_RECTS);
    }
}

// ============================ Q METRIC ============================

static double quality(const vector<PlacedBay>& sol, double wh_area) {
    if (sol.empty()) return 1e100;
    double price = 0, loads = 0, area = 0;
    for (auto& p : sol) {
        price += p.price;
        loads += p.loads;
        area += (double)p.w * p.d;
    }
    double base = price / max(1.0, loads);
    double exponent = 2.0 - (area / wh_area);
    return pow(base, exponent);
}

// ============================ DRIVER ============================

static vector<PlacedBay> run_maxrects(
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    OrderMode order_mode,
    FitStrategy fit
) {
    vector<PlacedBay> sol;
    vector<FreeRect> rects = initial_free_rects(warehouse, obstacles);

    vector<int> order(types.size());
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b) {
        return bay_order_score(types[a], order_mode, wh_area) >
               bay_order_score(types[b], order_mode, wh_area);
    });

    // Greedy loop: place the best (type, rotation, free-rect) at each step
    // until nothing fits. Each iteration is O(F * T * 2).
    while (true) {
        CandidatePlacement p = find_best_placement(rects, types, order,
                                                   warehouse, ceiling, fit);
        if (!p.valid) break;

        const BayType& t = types[p.type_index];
        PlacedBay pb;
        pb.type_id = t.id;
        pb.x = p.x;
        pb.y = p.y;
        pb.w = (int)p.bay_w;
        pb.d = (int)p.bay_d;
        pb.rotation = p.rotation;
        pb.loads = t.loads;
        pb.price = t.price;
        sol.push_back(pb);

        apply_placement(rects, p);
        if (rects.empty()) break;
    }

    return sol;
}

static void write_solution(const string& path, const vector<PlacedBay>& sol) {
    ofstream f(path);
    f << "Id,X,Y,Rotation\n";
    for (auto& p : sol) {
        f << p.type_id << ","
          << (long long)llround(p.x) << ","
          << (long long)llround(p.y) << ","
          << p.rotation << "\n";
    }
}

static void solve_case(const string& dir) {
    auto t0 = Clock::now();
    cout << "\n=== " << dir << " ===\n";

    auto warehouse = read_warehouse(dir + "/warehouse.csv");
    if (warehouse.size() < 3) { cout << "missing warehouse\n"; return; }
    auto obstacles = read_obstacles(dir + "/obstacles.csv");
    auto ceiling = read_ceiling(dir + "/ceiling.csv");
    auto types = read_bays(dir + "/types_of_bays.csv");
    if (types.empty()) { cout << "no bay types\n"; return; }

    double total_area = polygon_area(warehouse);
    double obs_area = 0;
    for (auto& o : obstacles) obs_area += o.w * o.d;
    double wh_area = max(1.0, total_area - obs_area);

    cout << "wh_area=" << wh_area
         << " types=" << types.size()
         << " obstacles=" << obstacles.size() << "\n";

    auto deadline = Clock::now() + chrono::milliseconds(
        (long long)(CASE_BUDGET_SECONDS * 1000));

    vector<PlacedBay> best;
    double best_q = 1e100;
    string best_label;

    // Try every (order, fit) pair; first complete pass is the priority. After
    // that we keep cycling until the deadline. Each pass is fully deterministic
    // for a given (order, fit) so re-running it is wasted; we only iterate the
    // distinct combinations once (16 total) which is fast.
    for (int om = 0; om < NUM_ORDER_MODES; om++) {
        for (int fs = 0; fs < NUM_FIT_STRATEGIES; fs++) {
            if (Clock::now() > deadline) break;
            auto sol = run_maxrects(types, warehouse, obstacles, ceiling,
                                    wh_area, (OrderMode)om, (FitStrategy)fs);
            double q = quality(sol, wh_area);
            cout << "order=" << om << " fit=" << fs
                 << " bays=" << sol.size() << " Q=" << q << "\n";
            if (q < best_q) {
                best_q = q;
                best = sol;
                ostringstream lbl;
                lbl << "order=" << om << " fit=" << fs;
                best_label = lbl.str();
            }
        }
    }

    write_solution(dir + "/solution.csv", best);

    double price = 0, loads = 0, area = 0;
    for (auto& p : best) { price += p.price; loads += p.loads; area += (double)p.w * p.d; }
    cout << "BEST [" << best_label << "] bays=" << best.size()
         << " area=" << area << " loads=" << loads << " price=" << price
         << " Q=" << best_q << "\n";
    cout << "[time] " << dir << " "
         << chrono::duration<double>(Clock::now() - t0).count() << "s\n";
}

int main() {
    auto t0 = Clock::now();
    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");
        if (f.good()) solve_case(c);
        else cout << "skip " << c << "\n";
    }
    cout << "\n[time] total "
         << chrono::duration<double>(Clock::now() - t0).count() << "s\n";
    return 0;
}
