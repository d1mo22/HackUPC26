#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>
#include <fstream>
#include <tuple>
#include <numeric>
#include <string>

using namespace std;

const vector<string> CASES = {"Case0", "Case1", "Case2", "Case3"};

const int ITERATIONS = 200;
const int INITIAL_ADDS = 50;
const int RESTARTS = 2;
const int MAX_CANDIDATES_TO_VALIDATE = 120;
const int ANGLE_SAMPLE = 6;

const double EPS = 1e-7;
const double PI = acos(-1.0);

mt19937 rng(42);
ofstream dataset;

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

vector<int> ANGLES = {0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330};


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

    for (auto p : poly) {
        double v = dotp(p, axis);
        mn = min(mn, v);
        mx = max(mx, v);
    }
}

bool sat_overlap_positive_area(const vector<Point>& a, const vector<Point>& b) {
    vector<vector<Point>> polys = {a, b};

    for (auto& poly : polys) {
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
                return false;
            }
        }
    }

    return true;
}

bool polygons_overlap_area(const vector<Point>& a, const vector<Point>& b) {
    return sat_overlap_positive_area(a, b);
}

vector<Point> make_rotated_rect(double x, double y, double w, double d, int angle) {
    double rad = angle * PI / 180.0;
    double ca = cos(rad);
    double sa = sin(rad);

    vector<Point> local = {
        {0, 0},
        {w, 0},
        {w, d},
        {0, d}
    };

    vector<Point> res;

    for (auto p : local) {
        double rx = p.x * ca - p.y * sa;
        double ry = p.x * sa + p.y * ca;
        res.push_back({x + rx, y + ry});
    }

    return res;
}

vector<Point> gap_poly_values(double x, double y, int w, int d, int gap, int angle) {
    if (gap <= 0) return {};

    double rad = angle * PI / 180.0;
    double ca = cos(rad);
    double sa = sin(rad);

    vector<Point> local = {
        {0, (double)d},
        {(double)w, (double)d},
        {(double)w, (double)d + gap},
        {0, (double)d + gap}
    };

    vector<Point> res;

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

vector<vector<Point>> all_polys(const PlacedBay& p) {
    vector<vector<Point>> res;
    res.push_back(bay_poly(p));

    auto g = gap_poly(p);
    if (!g.empty()) res.push_back(g);

    return res;
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


// ================= CSV READ =================

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
            res.push_back({stod(v[0]), stod(v[1]), stod(v[2]), stod(v[3])});
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
                stoi(v[0]), stoi(v[1]), stoi(v[2]),
                stoi(v[3]), stoi(v[4]), stoi(v[5]), stoi(v[6])
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
    vector<vector<Point>> polys;

    vector<Point> bay = make_rotated_rect(x, y, w, d, angle);
    polys.push_back(bay);

    auto gp = gap_poly_values(x, y, w, d, gap, angle);
    if (!gp.empty()) polys.push_back(gp);

    for (auto& poly : polys) {
        if (!polygon_inside_polygon(poly, warehouse)) {
            return false;
        }
    }

    for (auto& poly : polys) {
        for (auto& o : obstacles) {
            auto op = obstacle_poly(o);

            if (polygons_overlap_area(poly, op)) {
                return false;
            }
        }
    }

    for (auto& poly : polys) {
        for (int i = 0; i < (int)sol.size(); i++) {
            if (i == ignore) continue;

            for (auto& other : all_polys(sol[i])) {
                if (polygons_overlap_area(poly, other)) {
                    return false;
                }
            }
        }
    }

    auto bx = minmax_x(bay);
    double min_h = min_ceiling_between(bx.first, bx.second, ceiling);

    if (h > min_h + EPS) {
        return false;
    }

    return true;
}


// ================= SCORE / FEATURES =================

double quality(const vector<PlacedBay>& sol, double warehouse_area) {
    if (sol.empty()) return 1e100;

    double price_load_sum = 0;
    double used_area = 0;

    for (auto& p : sol) {
        price_load_sum += (double)p.price / max(1, p.loads);
        used_area += (double)p.w * p.d;
    }

    double exponent = 2.0 - used_area / warehouse_area;

    return pow(price_load_sum, exponent);
}

double used_area_ratio(const vector<PlacedBay>& sol, double wh_area) {
    double used = 0.0;

    for (auto& p : sol) {
        used += (double)p.w * p.d;
    }

    return used / wh_area;
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

double heuristic_score(
    const BayType& t,
    double x,
    double y,
    int angle,
    const vector<PlacedBay>& sol,
    double wh_area
) {
    double area = (double)t.w * t.d;
    double cost_load = (double)t.price / max(1, t.loads);
    double efficiency = ((double)t.loads * area) / max(1.0, (double)t.price);
    double gap_ratio = (double)t.gap / max(1, t.d);

    double s = 0.0;

    s += 2.5 * efficiency;
    s += 1.8 * area / wh_area;
    s -= 1.4 * cost_load;
    s -= 0.8 * gap_ratio;
    s -= 0.00002 * (x + y);

    if (angle == 0 || angle == 90 || angle == 180 || angle == 270) {
        s += 0.5;
    }

    if ((int)sol.size() > 20) {
        s -= 0.00002 * area;
    }

    return s;
}

void write_dataset_row(
    const BayType& t,
    double x,
    double y,
    int angle,
    const vector<PlacedBay>& sol,
    double wh_area,
    bool valid,
    int label
) {
    double area = (double)t.w * t.d;
    double ratio = (double)t.w / max(1, t.d);
    double efficiency = ((double)t.loads * area) / max(1.0, (double)t.price);
    double gap_ratio = (double)t.gap / max(1, t.d);
    int n_bays = (int)sol.size();
    double used_ratio = used_area_ratio(sol, wh_area);

    dataset << area << ","
            << ratio << ","
            << efficiency << ","
            << gap_ratio << ","
            << angle << ","
            << x << ","
            << y << ","
            << n_bays << ","
            << used_ratio << ","
            << (valid ? 1 : 0) << ","
            << label << "\n";
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
        for (auto& poly : all_polys(p)) {
            for (auto q : poly) {
                pts.push_back({round(q.x), round(q.y)});
            }
        }
    }

    shuffle(pts.begin(), pts.end(), rng);

    if ((int)pts.size() > 250) pts.resize(250);

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
    bool collect_data = true
) {
    auto pts = candidate_points(warehouse, obstacles, sol);

    struct Candidate {
        double score;
        double x, y;
        BayType t;
        int angle;
    };

    vector<Candidate> candidates;

    for (auto& p : pts) {
        for (auto& t : types) {
            vector<int> angles = ANGLES;
            shuffle(angles.begin(), angles.end(), rng);

            int limit = min(ANGLE_SAMPLE, (int)angles.size());

            for (int i = 0; i < limit; i++) {
                int angle = angles[i];

                double s = heuristic_score(t, p.x, p.y, angle, sol, wh_area);
                candidates.push_back({s, p.x, p.y, t, angle});
            }
        }
    }

    sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });

    int limit = min(MAX_CANDIDATES_TO_VALIDATE, (int)candidates.size());

    PlacedBay best;
    bool found = false;
    double best_q = 1e100;

    double q_old = quality(sol, wh_area);

    for (int i = 0; i < limit; i++) {
        auto& c = candidates[i];

        bool valid = valid_candidate(
            c.x, c.y,
            c.t.w, c.t.d,
            c.t.h, c.t.gap,
            c.angle,
            sol, warehouse, obstacles, ceiling
        );

        int label = 0;

        if (valid) {
            PlacedBay p = make_candidate(c.t, c.x, c.y, c.angle);

            sol.push_back(p);
            double q_new = quality(sol, wh_area);
            sol.pop_back();

            if (q_new < q_old) {
                label = 1;
            }

            if (q_new < best_q) {
                best_q = q_new;
                best = p;
                found = true;
            }
        }

        if (collect_data) {
            write_dataset_row(c.t, c.x, c.y, c.angle, sol, wh_area, valid, label);
        }
    }

    if (found) {
        sol.push_back(best);
        return true;
    }

    return false;
}

void delete_bay(vector<PlacedBay>& sol) {
    if (sol.empty()) return;

    int idx = rng() % sol.size();
    sol.erase(sol.begin() + idx);
}

void replace_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area
) {
    if (sol.empty()) return;

    int idx = rng() % sol.size();
    PlacedBay old = sol[idx];

    sol.erase(sol.begin() + idx);

    bool ok = add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, true);

    if (!ok) {
        sol.push_back(old);
    }
}

void fill_aggressive(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area
) {
    for (int i = 0; i < 3; i++) {
        bool ok = add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, true);
        if (!ok) break;
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
                p.x, p.y, p.w, p.d,
                p.h, p.gap, p.angle,
                sol, warehouse, obstacles, ceiling, i
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
    double wh_area
) {
    vector<PlacedBay> sol;

    for (int i = 0; i < INITIAL_ADDS; i++) {
        add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, true);
    }

    return sol;
}

vector<PlacedBay> hill(
    vector<PlacedBay> sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area
) {
    vector<PlacedBay> best = sol;
    double best_q = quality(best, wh_area);

    for (int it = 0; it < ITERATIONS; it++) {
        vector<PlacedBay> candidate = best;

        int op = rng() % 4;

        if (op == 0) {
            add_bay(candidate, types, warehouse, obstacles, ceiling, wh_area, true);
        } else if (op == 1) {
            replace_bay(candidate, types, warehouse, obstacles, ceiling, wh_area);
        } else if (op == 2) {
            delete_bay(candidate);
        } else {
            fill_aggressive(candidate, types, warehouse, obstacles, ceiling, wh_area);
        }

        double q = quality(candidate, wh_area);

        if (q < best_q) {
            best = candidate;
            best_q = q;
        }
    }

    return best;
}

void solve_case(const string& case_dir) {
    cout << "\n=== Collecting " << case_dir << " ===\n";

    auto warehouse = read_warehouse(case_dir + "/warehouse.csv");
    auto obstacles = read_obstacles(case_dir + "/obstacles.csv");
    auto ceiling = read_ceiling(case_dir + "/ceiling.csv");
    auto types = read_bays(case_dir + "/types_of_bays.csv");

    double wh_area = polygon_area(warehouse);

    vector<PlacedBay> best_global;
    double best_global_q = 1e100;

    for (int r = 0; r < RESTARTS; r++) {
        cout << "Restart " << r + 1 << "/" << RESTARTS << "\n";

        auto sol = build_initial(types, warehouse, obstacles, ceiling, wh_area);
        sol = hill(sol, types, warehouse, obstacles, ceiling, wh_area);

        auto [a, l, pr, q] = details(sol, wh_area);
        bool valid = is_valid_solution(sol, warehouse, obstacles, ceiling);

        cout << "Collected solution -> bays=" << sol.size()
             << " area=" << a
             << " loads=" << l
             << " price=" << pr
             << " Q=" << q
             << " valid=" << valid << "\n";

        if (valid && q < best_global_q) {
            best_global = sol;
            best_global_q = q;
        }
    }

    string out_path = case_dir + "/solution_collect.csv";
    ofstream out(out_path);

    out << "Id,X,Y,Rotation\n";

    for (auto& p : best_global) {
        out << p.id << ","
            << llround(p.x) << ","
            << llround(p.y) << ","
            << p.angle << "\n";
    }

    out.close();

    cout << "Written: " << out_path << "\n";
}

int main() {
    dataset.open("dataset.csv");

    dataset << "area,ratio,efficiency,gap_ratio,angle,x,y,n_bays,used_area_ratio,valid,label\n";

    for (auto& c : CASES) {
        ifstream f(c + "/warehouse.csv");

        if (f.good()) {
            solve_case(c);
        } else {
            cout << "Skipping " << c << "\n";
        }
    }

    dataset.close();

    cout << "\nDataset written: dataset.csv\n";

    return 0;
}