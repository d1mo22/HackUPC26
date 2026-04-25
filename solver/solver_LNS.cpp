/*
 * WAREHOUSE OPTIMIZER — HackUPC 2026
 * ====================================
 * Compile:  g++ -O3 -march=native -fopenmp -std=c++17 -o optimizer solverEudald.cpp
 * Run:      OMP_NUM_THREADS=8 ./optimizer warehouse.csv obstacles.csv ceiling.csv types_of_bays.csv
 * Output:   solution.csv
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>
#include <limits>
#include <numeric>
#include <iomanip>
#include <cstdint>
#include <omp.h>

using namespace std;

// ─────────────────────────────────────────────
//  CONSTANTS
// ─────────────────────────────────────────────

const double EPS = 1e-7;
const double PI  = acos(-1.0);

// Angles to try (multiples of 10°, only 0–170 because 180=0 for a rect)
const vector<int> ANGLES = {0,10,20,30,40,50,60,70,80,90,100,110,120,130,140,150,160,170};

const int INITIAL_ADDS  = 70;
const int RESTARTS      = 8;
const int MAX_PTS_ADD   = 60;   // ligeramente menor para ir más rápido
const int ANGLE_SAMPLE  = 6;    // ligeramente menor para ir más rápido

// LNS params
const int LNS_ITERATIONS = 320; // algo menos que 500 para acelerar
const int LNS_K_MIN = 2;
const int LNS_K_MAX = 8;
const int LNS_REPAIR_EXTRA = 2;
const double LNS_SA_T0 = 0.02;
const double LNS_SA_ALPHA = 0.995;

// ─────────────────────────────────────────────
//  RNG helper (thread-local by passing reference)
// ─────────────────────────────────────────────

static inline mt19937 make_rng(unsigned seed_offset = 0) {
    uint64_t t = chrono::high_resolution_clock::now().time_since_epoch().count();
    uint64_t s = t ^ (0x9e3779b97f4a7c15ULL + (uint64_t)seed_offset * 0xbf58476d1ce4e5b9ULL);
    return mt19937((uint32_t)(s ^ (s >> 32)));
}

// ─────────────────────────────────────────────
//  DATA STRUCTURES
// ─────────────────────────────────────────────

struct Point { double x, y; };

struct BayType {
    int id, w, d, h, gap, loads, price;
};

struct PlacedBay {
    int    id;
    double x, y;
    int    w, d, h, gap;
    int    angle;
    int    price, loads;
};

struct Obstacle { double x, y, w, d; };

// ─────────────────────────────────────────────
//  GEOMETRY
// ─────────────────────────────────────────────

double cross2(Point a, Point b, Point c) {
    return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
}
double dotp(Point a, Point b) { return a.x*b.x + a.y*b.y; }

double polygon_area(const vector<Point>& p) {
    double a = 0;
    for (int i = 0; i < (int)p.size(); i++) {
        Point p1 = p[i], p2 = p[(i+1)%p.size()];
        a += p1.x*p2.y - p2.x*p1.y;
    }
    return fabs(a)/2.0;
}

bool point_on_segment(Point p, Point a, Point b) {
    if (fabs(cross2(a,b,p)) > EPS) return false;
    return min(a.x,b.x)-EPS <= p.x && p.x <= max(a.x,b.x)+EPS &&
           min(a.y,b.y)-EPS <= p.y && p.y <= max(a.y,b.y)+EPS;
}

int orient(Point a, Point b, Point c) {
    double v = cross2(a,b,c);
    if (fabs(v) < EPS) return 0;
    return v > 0 ? 1 : -1;
}

bool proper_segment_intersection(Point a, Point b, Point c, Point d) {
    return orient(a,b,c)*orient(a,b,d) < 0 &&
           orient(c,d,a)*orient(c,d,b) < 0;
}

bool point_inside_or_on_polygon(Point p, const vector<Point>& poly) {
    for (int i = 0; i < (int)poly.size(); i++)
        if (point_on_segment(p, poly[i], poly[(i+1)%poly.size()])) return true;
    bool inside = false;
    for (int i = 0, j = (int)poly.size()-1; i < (int)poly.size(); j=i++) {
        Point a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            double xi = (b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x;
            if (p.x < xi) inside = !inside;
        }
    }
    return inside;
}

bool any_proper_segment_crossing(const vector<Point>& a, const vector<Point>& b) {
    for (int i = 0; i < (int)a.size(); i++) {
        Point a1 = a[i], a2 = a[(i+1)%a.size()];
        for (int j = 0; j < (int)b.size(); j++) {
            Point b1 = b[j], b2 = b[(j+1)%b.size()];
            if (proper_segment_intersection(a1,a2,b1,b2)) return true;
        }
    }
    return false;
}

bool polygon_inside_polygon(const vector<Point>& small, const vector<Point>& big) {
    for (auto p : small)
        if (!point_inside_or_on_polygon(p, big)) return false;
    if (any_proper_segment_crossing(small, big)) return false;
    return true;
}

void project_poly(const vector<Point>& poly, Point axis, double& mn, double& mx) {
    mn = mx = dotp(poly[0], axis);
    for (auto p : poly) { double v=dotp(p,axis); mn=min(mn,v); mx=max(mx,v); }
}

// SAT overlap (touching boundary = NOT overlapping)
bool sat_overlap_positive_area(const vector<Point>& a, const vector<Point>& b) {
    for (auto& poly : {a, b}) {
        for (int i = 0; i < (int)poly.size(); i++) {
            Point p1 = poly[i], p2 = poly[(i+1)%poly.size()];
            Point edge = {p2.x-p1.x, p2.y-p1.y};
            Point axis = {-edge.y, edge.x};
            double len = hypot(axis.x, axis.y);
            if (len < EPS) continue;
            axis.x /= len; axis.y /= len;
            double minA,maxA,minB,maxB;
            project_poly(a, axis, minA, maxA);
            project_poly(b, axis, minB, maxB);
            if (maxA <= minB+EPS || maxB <= minA+EPS) return false;
        }
    }
    return true;
}

bool polygons_overlap_area(const vector<Point>& a, const vector<Point>& b) {
    return sat_overlap_positive_area(a, b);
}

vector<Point> make_rotated_rect(double x, double y, double w, double d, int angle) {
    double rad = angle * PI / 180.0;
    double ca = cos(rad), sa = sin(rad);
    vector<Point> local = {{0,0},{w,0},{w,d},{0,d}};
    vector<Point> res;
    for (auto p : local)
        res.push_back({x + p.x*ca - p.y*sa, y + p.x*sa + p.y*ca});
    return res;
}

vector<Point> gap_poly_values(double x, double y, int w, int d, int gap, int angle) {
    if (gap <= 0) return {};
    double rad = angle * PI / 180.0;
    double ca = cos(rad), sa = sin(rad);
    vector<Point> local = {{0,(double)d},{(double)w,(double)d},
                           {(double)w,(double)d+gap},{0,(double)d+gap}};
    vector<Point> res;
    for (auto p : local)
        res.push_back({x + p.x*ca - p.y*sa, y + p.x*sa + p.y*ca});
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
    return {{o.x,o.y},{o.x+o.w,o.y},{o.x+o.w,o.y+o.d},{o.x,o.y+o.d}};
}

pair<double,double> minmax_x(const vector<Point>& poly) {
    double mn=poly[0].x, mx=poly[0].x;
    for (auto p : poly) { mn=min(mn,p.x); mx=max(mx,p.x); }
    return {mn,mx};
}

// ─────────────────────────────────────────────
//  CSV PARSING
// ─────────────────────────────────────────────

vector<string> split_csv(const string& line) {
    vector<string> out; string cur;
    for (char c : line) {
        if (c==',') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    for (auto& s : out) {
        while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && isspace((unsigned char)s.back()))  s.pop_back();
    }
    return out;
}

bool file_empty_or_missing(const string& path) {
    ifstream f(path);
    if (!f.good()) return true;
    string line;
    while (getline(f,line))
        for (char c : line) if (!isspace((unsigned char)c)) return false;
    return true;
}

vector<Point> read_warehouse(const string& path) {
    vector<Point> res; ifstream f(path); string line;
    while (getline(f,line)) {
        if (line.empty()) continue;
        auto v = split_csv(line);
        if (v.size()>=2) res.push_back({stod(v[0]),stod(v[1])});
    }
    return res;
}

vector<Obstacle> read_obstacles(const string& path) {
    vector<Obstacle> res;
    if (file_empty_or_missing(path)) return res;
    ifstream f(path); string line;
    while (getline(f,line)) {
        if (line.empty()) continue;
        auto v = split_csv(line);
        if (v.size()>=4) res.push_back({stod(v[0]),stod(v[1]),stod(v[2]),stod(v[3])});
    }
    return res;
}

vector<pair<double,double>> read_ceiling(const string& path) {
    vector<pair<double,double>> res; ifstream f(path); string line;
    while (getline(f,line)) {
        if (line.empty()) continue;
        auto v = split_csv(line);
        if (v.size()>=2) res.push_back({stod(v[0]),stod(v[1])});
    }
    sort(res.begin(),res.end());
    return res;
}

vector<BayType> read_bays(const string& path) {
    vector<BayType> res; ifstream f(path); string line;
    while (getline(f,line)) {
        if (line.empty()) continue;
        auto v = split_csv(line);
        if (v.size()>=7)
            res.push_back({stoi(v[0]),stoi(v[1]),stoi(v[2]),stoi(v[3]),
                           stoi(v[4]),stoi(v[5]),stoi(v[6])});
    }
    return res;
}

// ─────────────────────────────────────────────
//  VALIDATION
// ─────────────────────────────────────────────

double min_ceiling_between(double x1, double x2,
                           const vector<pair<double,double>>& ceiling) {
    if (ceiling.empty()) return 1e18;
    if (x1>x2) swap(x1,x2);
    double ans = 1e18;
    for (int i=0;i<(int)ceiling.size();i++) {
        double sx1=ceiling[i].first, h=ceiling[i].second;
        double sx2 = (i+1<(int)ceiling.size()) ? ceiling[i+1].first : 1e18;
        double left=max(x1,sx1), right=min(x2,sx2);
        if (left < right+EPS) ans=min(ans,h);
    }
    return ans;
}

bool valid_candidate(
    double x, double y, int w, int d, int h, int gap, int angle,
    const vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    int ignore = -1)
{
    vector<vector<Point>> polys;
    auto bay = make_rotated_rect(x,y,w,d,angle);
    polys.push_back(bay);
    auto gp = gap_poly_values(x,y,w,d,gap,angle);
    if (!gp.empty()) polys.push_back(gp);

    for (auto& poly : polys)
        if (!polygon_inside_polygon(poly, warehouse)) return false;

    for (auto& poly : polys)
        for (auto& o : obstacles)
            if (polygons_overlap_area(poly, obstacle_poly(o))) return false;

    for (auto& poly : polys)
        for (int i=0;i<(int)sol.size();i++) {
            if (i==ignore) continue;
            for (auto& other : all_polys(sol[i]))
                if (polygons_overlap_area(poly, other)) return false;
        }

    auto bx = minmax_x(bay);
    if (h > min_ceiling_between(bx.first, bx.second, ceiling)+EPS) return false;

    return true;
}

// ─────────────────────────────────────────────
//  OBJECTIVE
// ─────────────────────────────────────────────

double quality(const vector<PlacedBay>& sol, double wh_area) {
    if (sol.empty()) return 1e100;
    double pl=0, area=0;
    for (auto& p : sol) {
        pl   += (double)p.price / max(1,p.loads);
        area += (double)p.w * p.d;
    }
    return pow(pl, 2.0 - area/wh_area);
}

double bay_score(const BayType& t) {
    double reserved = (double)t.w * (t.d + t.gap);
    return ((double)t.loads * t.w * t.d) / max(1.0, (double)t.price * reserved);
}

// ─────────────────────────────────────────────
//  OPERATORS
// ─────────────────────────────────────────────

vector<Point> candidate_points(
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<PlacedBay>& sol,
    mt19937& rng)
{
    vector<Point> pts;
    pts.reserve(300);

    for (auto p : warehouse) pts.push_back(p);
    for (auto& o : obstacles) {
        pts.push_back({o.x,o.y});         pts.push_back({o.x+o.w,o.y});
        pts.push_back({o.x,o.y+o.d});     pts.push_back({o.x+o.w,o.y+o.d});
    }
    for (auto& p : sol)
        for (auto& poly : all_polys(p))
            for (auto q : poly)
                pts.push_back({round(q.x),round(q.y)});

    shuffle(pts.begin(),pts.end(),rng);
    if ((int)pts.size()>250) pts.resize(250);
    return pts;
}

PlacedBay make_candidate(const BayType& t, double x, double y, int angle) {
    return {t.id, x, y, t.w, t.d, t.h, t.gap, angle, t.price, t.loads};
}

bool add_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    mt19937& rng)
{
    auto pts = candidate_points(warehouse, obstacles, sol, rng);

    vector<BayType> sorted_types = types;
    sort(sorted_types.begin(),sorted_types.end(),[](const BayType& a,const BayType& b){
        return bay_score(a)>bay_score(b);
    });

    PlacedBay best; bool found=false; double best_q=1e100;
    int plimit = min(MAX_PTS_ADD,(int)pts.size());

    for (int pi=0;pi<plimit;pi++) {
        double x=pts[pi].x, y=pts[pi].y;
        for (auto& t : sorted_types) {
            vector<int> angles=ANGLES;
            shuffle(angles.begin(),angles.end(),rng);
            int alimit=min(ANGLE_SAMPLE,(int)angles.size());
            for (int ai=0;ai<alimit;ai++) {
                int angle=angles[ai];
                if (valid_candidate(x,y,t.w,t.d,t.h,t.gap,angle,
                                    sol,warehouse,obstacles,ceiling)) {
                    auto c=make_candidate(t,x,y,angle);
                    sol.push_back(c);
                    double q=quality(sol,wh_area);
                    sol.pop_back();
                    if (q<best_q) { best_q=q; best=c; found=true; }
                }
            }
        }
    }
    if (found) { sol.push_back(best); return true; }
    return false;
}

// ─────────────────────────────────────────────
//  LNS
// ─────────────────────────────────────────────

double overlap_tendency_score(
    int idx,
    const vector<PlacedBay>& sol,
    const vector<Obstacle>& obstacles)
{
    auto poly_i = bay_poly(sol[idx]);
    auto mmx_i = minmax_x(poly_i);

    double score = 0.0;
    score += (double)sol[idx].price / max(1, sol[idx].loads);

    for (int j = 0; j < (int)sol.size(); j++) {
        if (j == idx) continue;
        auto poly_j = bay_poly(sol[j]);
        auto mmx_j = minmax_x(poly_j);

        double distX = 0.0;
        if (mmx_i.second < mmx_j.first) distX = mmx_j.first - mmx_i.second;
        else if (mmx_j.second < mmx_i.first) distX = mmx_i.first - mmx_j.second;
        else distX = 0.0;

        score += 1.0 / (1.0 + distX);
    }

    for (auto& o : obstacles) {
        double ox1 = o.x, ox2 = o.x + o.w;
        double distX = 0.0;
        if (mmx_i.second < ox1) distX = ox1 - mmx_i.second;
        else if (ox2 < mmx_i.first) distX = mmx_i.first - ox2;
        else distX = 0.0;
        score += 0.5 / (1.0 + distX);
    }

    return score;
}

void destroy_guided(
    vector<PlacedBay>& sol,
    const vector<Obstacle>& obstacles,
    int k,
    mt19937& rng)
{
    if (sol.empty() || k <= 0) return;
    k = min(k, (int)sol.size());

    vector<pair<double,int>> rank;
    rank.reserve(sol.size());
    for (int i = 0; i < (int)sol.size(); i++) {
        double s = overlap_tendency_score(i, sol, obstacles);
        rank.push_back({s, i});
    }
    sort(rank.begin(), rank.end(), [](auto& a, auto& b){ return a.first > b.first; });

    int pool = min((int)rank.size(), max(k*2, k));
    vector<int> cand_idx;
    cand_idx.reserve(pool);
    for (int i = 0; i < pool; i++) cand_idx.push_back(rank[i].second);
    shuffle(cand_idx.begin(), cand_idx.end(), rng);

    vector<char> del(sol.size(), 0);
    for (int i = 0; i < k; i++) del[cand_idx[i]] = 1;

    vector<PlacedBay> ns;
    ns.reserve(sol.size() - k);
    for (int i = 0; i < (int)sol.size(); i++)
        if (!del[i]) ns.push_back(sol[i]);

    sol.swap(ns);
}

void repair_greedy(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    int attempts,
    mt19937& rng)
{
    for (int i = 0; i < attempts; i++) {
        if (!add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, rng)) break;
    }
}

vector<PlacedBay> lns(
    vector<PlacedBay> sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    mt19937& rng)
{
    vector<PlacedBay> current = sol;
    double current_q = quality(current, wh_area);

    vector<PlacedBay> best = current;
    double best_q = current_q;

    double T = LNS_SA_T0;
    uniform_real_distribution<double> U(0.0, 1.0);
    uniform_int_distribution<int> Kdist(LNS_K_MIN, LNS_K_MAX);

    for (int it = 0; it < LNS_ITERATIONS; it++) {
        vector<PlacedBay> cand = current;

        int k = Kdist(rng);
        destroy_guided(cand, obstacles, k, rng);
        repair_greedy(cand, types, warehouse, obstacles, ceiling, wh_area, k + LNS_REPAIR_EXTRA, rng);

        double q = quality(cand, wh_area);

        bool accept = false;
        if (q < current_q) {
            accept = true;
        } else {
            double delta = q - current_q;
            double p = exp(-delta / max(1e-12, T));
            if (U(rng) < p) accept = true;
        }

        if (accept) {
            current = cand;
            current_q = q;
            if (current_q < best_q) {
                best = current;
                best_q = current_q;
            }
        }

        T *= LNS_SA_ALPHA;
    }

    return best;
}

// ─────────────────────────────────────────────
//  SOLVER
// ─────────────────────────────────────────────

bool is_valid_solution(
    const vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling)
{
    for (int i=0;i<(int)sol.size();i++) {
        auto& p=sol[i];
        if (!valid_candidate(p.x,p.y,p.w,p.d,p.h,p.gap,p.angle,
                             sol,warehouse,obstacles,ceiling,i)) {
            return false;
        }
    }
    return true;
}

vector<PlacedBay> build_initial(
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    mt19937& rng)
{
    vector<PlacedBay> sol;
    for (int i=0;i<INITIAL_ADDS;i++)
        add_bay(sol,types,warehouse,obstacles,ceiling,wh_area,rng);
    return sol;
}

// ─────────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 5) {
        cerr << "Usage: ./optimizer warehouse.csv obstacles.csv ceiling.csv types_of_bays.csv\n";
        return 1;
    }

    auto warehouse = read_warehouse(argv[1]);
    auto obstacles = read_obstacles(argv[2]);
    auto ceiling   = read_ceiling(argv[3]);
    auto types     = read_bays(argv[4]);

    double wh_area = polygon_area(warehouse);
    cout << "Warehouse area: " << wh_area << " mm²\n";

    vector<PlacedBay> best_global;
    double best_global_q = 1e100;

    // Parallel restarts
    #pragma omp parallel
    {
        mt19937 trng = make_rng((unsigned)omp_get_thread_num() + 12345);

        vector<PlacedBay> best_local;
        double best_local_q = 1e100;

        #pragma omp for schedule(dynamic)
        for (int r=0; r<RESTARTS; r++) {
            auto sol = build_initial(types,warehouse,obstacles,ceiling,wh_area,trng);
            sol = lns(sol,types,warehouse,obstacles,ceiling,wh_area,trng);

            double q = quality(sol,wh_area);
            bool valid = is_valid_solution(sol,warehouse,obstacles,ceiling);

            // opcional: log mínimo (sin mezclar demasiado salida)
            #pragma omp critical
            {
                cout << "Restart " << r+1 << "/" << RESTARTS
                     << " -> bays=" << sol.size()
                     << " Q=" << fixed << setprecision(6) << q
                     << " valid=" << valid
                     << " thread=" << omp_get_thread_num() << "\n";
            }

            if (valid && q < best_local_q) {
                best_local = sol;
                best_local_q = q;
            }
        }

        #pragma omp critical
        {
            if (best_local_q < best_global_q) {
                best_global = best_local;
                best_global_q = best_local_q;
            }
        }
    }

    if (!best_global.empty() && !is_valid_solution(best_global,warehouse,obstacles,ceiling))
        best_global.clear();

    ofstream out("solution.csv");
    out << "Id,X,Y,Rotation\n";
    for (auto& p : best_global)
        out << p.id << "," << llround(p.x) << "," << llround(p.y) << "," << p.angle << "\n";
    out.close();

    cout << "\nsolution.csv written (" << best_global.size() << " bays)\n";
    cout << "Q = " << fixed << setprecision(6) << best_global_q << "\n";

    return 0;
}