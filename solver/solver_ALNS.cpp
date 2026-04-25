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
const int RESTARTS      = 16;
const int MAX_PTS_ADD   = 80;
const int ANGLE_SAMPLE  = 8;

// ALNS params
const int LNS_ITERATIONS = 700;
const int LNS_K_MIN = 2;
const int LNS_K_MAX = 8;
const int LNS_REPAIR_EXTRA = 2;
const double LNS_SA_T0 = 0.03;
const double LNS_SA_ALPHA = 0.997;

// intensification phase
const int INTENSIFY_ITER = 450;
const int INTENSIFY_K_MIN = 2;
const int INTENSIFY_K_MAX = 4;
const int INTENSIFY_ANGLE_SAMPLE = 12;
const int INTENSIFY_MAX_PTS_ADD = 100;
const double INTENSIFY_T0 = 0.01;
const double INTENSIFY_ALPHA = 0.998;

// ─────────────────────────────────────────────
//  RNG helper
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

pair<double,double> minmax_y(const vector<Point>& poly) {
    double mn=poly[0].y, mx=poly[0].y;
    for (auto p : poly) { mn=min(mn,p.y); mx=max(mx,p.y); }
    return {mn,mx};
}

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

bool aabb_overlap(const vector<Point>& a, const vector<Point>& b) {
    auto [min_xa, max_xa] = minmax_x(a);
    auto [min_xb, max_xb] = minmax_x(b);
    if (max_xa <= min_xb + EPS || max_xb <= min_xa + EPS) return false;

    auto [min_ya, max_ya] = minmax_y(a);
    auto [min_yb, max_yb] = minmax_y(b);
    if (max_ya <= min_yb + EPS || max_yb <= min_ya + EPS) return false;

    return true;
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
        for (auto& o : obstacles) {
            auto op = obstacle_poly(o);
            if (!aabb_overlap(poly, op)) continue;
            if (polygons_overlap_area(poly, op)) return false;
        }

    for (auto& poly : polys)
        for (int i=0;i<(int)sol.size();i++) {
            if (i==ignore) continue;
            for (auto& other : all_polys(sol[i])) {
                if (!aabb_overlap(poly, other)) continue;
                if (polygons_overlap_area(poly, other)) return false;
            }
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
    mt19937& rng,
    bool intensify_bias = false)
{
    vector<Point> pts;
    pts.reserve(400);

    for (auto p : warehouse) pts.push_back(p);
    for (auto& o : obstacles) {
        pts.push_back({o.x,o.y});         pts.push_back({o.x+o.w,o.y});
        pts.push_back({o.x,o.y+o.d});     pts.push_back({o.x+o.w,o.y+o.d});
    }
    for (auto& p : sol)
        for (auto& poly : all_polys(p))
            for (auto q : poly)
                pts.push_back({round(q.x),round(q.y)});

    // Mix 70/30 exploit/explore
    uniform_real_distribution<double> U(0.0, 1.0);
    bool do_sort = intensify_bias || (U(rng) < 0.70);

    if (do_sort) {
        sort(pts.begin(), pts.end(), [](const Point& a, const Point& b) {
            return (a.x + a.y) < (b.x + b.y);
        });
    } else {
        shuffle(pts.begin(), pts.end(), rng);
    }

    if ((int)pts.size() > 280) pts.resize(280);
    return pts;
}

PlacedBay make_candidate(const BayType& t, double x, double y, int angle) {
    return {t.id, x, y, t.w, t.d, t.h, t.gap, angle, t.price, t.loads};
}

bool add_bay_custom(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    mt19937& rng,
    int max_pts_add,
    int angle_sample,
    bool randomized_repair,
    bool intensify_bias)
{
    auto pts = candidate_points(warehouse, obstacles, sol, rng, intensify_bias);

    vector<BayType> sorted_types = types;
    sort(sorted_types.begin(),sorted_types.end(),[](const BayType& a,const BayType& b){
        return bay_score(a)>bay_score(b);
    });

    PlacedBay best; bool found=false; double best_q=1e100;
    int plimit = min(max_pts_add,(int)pts.size());

    uniform_int_distribution<int> pick_top(0, min(4, (int)sorted_types.size()-1)); // top-5

    for (int pi=0; pi<plimit; pi++) {
        double x=pts[pi].x, y=pts[pi].y;

        vector<int> type_order;
        if (!randomized_repair) {
            type_order.resize(sorted_types.size());
            iota(type_order.begin(), type_order.end(), 0);
        } else {
            // randomized: sample from top types + occasional global shuffle
            int tries = min((int)sorted_types.size(), 8);
            type_order.reserve(tries);
            for (int tt=0; tt<tries; tt++) type_order.push_back(pick_top(rng));
            if ((rng() % 10) == 0) { // 10%: add one random type globally
                type_order.push_back(rng() % sorted_types.size());
            }
        }

        for (int idx_t : type_order) {
            const auto& t = sorted_types[idx_t];

            vector<int> angles=ANGLES;
            shuffle(angles.begin(),angles.end(),rng);
            int alimit=min(angle_sample,(int)angles.size());

            for (int ai=0; ai<alimit; ai++) {
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

bool add_bay(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    mt19937& rng)
{
    return add_bay_custom(sol, types, warehouse, obstacles, ceiling, wh_area, rng,
                          MAX_PTS_ADD, ANGLE_SAMPLE, false, false);
}

// ─────────────────────────────────────────────
//  DESTROY OPERATORS (ALNS)
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

void destroy_guided(vector<PlacedBay>& sol, const vector<Obstacle>& obstacles, int k, mt19937& rng) {
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
    for (int i = 0; i < (int)sol.size(); i++) if (!del[i]) ns.push_back(sol[i]);
    sol.swap(ns);
}

void destroy_random(vector<PlacedBay>& sol, int k, mt19937& rng) {
    if (sol.empty() || k <= 0) return;
    k = min(k, (int)sol.size());

    vector<int> idx(sol.size());
    iota(idx.begin(), idx.end(), 0);
    shuffle(idx.begin(), idx.end(), rng);

    vector<char> del(sol.size(), 0);
    for (int i = 0; i < k; i++) del[idx[i]] = 1;

    vector<PlacedBay> ns;
    ns.reserve(sol.size() - k);
    for (int i = 0; i < (int)sol.size(); i++) if (!del[i]) ns.push_back(sol[i]);
    sol.swap(ns);
}

// fixed: worst = highest price/load
void destroy_worst(vector<PlacedBay>& sol, int k, mt19937& rng) {
    if (sol.empty() || k <= 0) return;
    k = min(k, (int)sol.size());

    vector<pair<double,int>> rank;
    rank.reserve(sol.size());
    for (int i = 0; i < (int)sol.size(); i++) {
        double badness = (double)sol[i].price / max(1, sol[i].loads);
        rank.push_back({badness, i});
    }

    sort(rank.begin(), rank.end(), [](auto& a, auto& b){ return a.first > b.first; }); // highest first

    int pool = min((int)rank.size(), max(k*2, k));
    vector<int> cand_idx;
    cand_idx.reserve(pool);
    for (int i = 0; i < pool; i++) cand_idx.push_back(rank[i].second);
    shuffle(cand_idx.begin(), cand_idx.end(), rng);

    vector<char> del(sol.size(), 0);
    for (int i = 0; i < k; i++) del[cand_idx[i]] = 1;

    vector<PlacedBay> ns;
    ns.reserve(sol.size() - k);
    for (int i = 0; i < (int)sol.size(); i++) if (!del[i]) ns.push_back(sol[i]);
    sol.swap(ns);
}

// ─────────────────────────────────────────────
//  REPAIR OPERATORS (ALNS)
// ─────────────────────────────────────────────

void repair_greedy(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    int attempts,
    mt19937& rng,
    int max_pts_add = MAX_PTS_ADD,
    int angle_sample = ANGLE_SAMPLE,
    bool intensify_bias = false)
{
    for (int i = 0; i < attempts; i++) {
        if (!add_bay_custom(sol, types, warehouse, obstacles, ceiling, wh_area, rng,
                            max_pts_add, angle_sample, false, intensify_bias)) break;
    }
}

void repair_randomized(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    int attempts,
    mt19937& rng,
    int max_pts_add = MAX_PTS_ADD,
    int angle_sample = ANGLE_SAMPLE,
    bool intensify_bias = false)
{
    for (int i = 0; i < attempts; i++) {
        if (!add_bay_custom(sol, types, warehouse, obstacles, ceiling, wh_area, rng,
                            max_pts_add, angle_sample, true, intensify_bias)) break;
    }
}

// ─────────────────────────────────────────────
//  ALNS CORE
// ─────────────────────────────────────────────

vector<PlacedBay> alns_core(
    vector<PlacedBay> sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    mt19937& rng,
    int iterations,
    int k_min,
    int k_max,
    int repair_extra,
    int angle_sample,
    int max_pts_add,
    double T0,
    double alpha,
    bool intensify_bias)
{
    vector<PlacedBay> current = sol;
    double current_q = quality(current, wh_area);

    vector<PlacedBay> best = current;
    double best_q = current_q;

    double T = T0;
    uniform_real_distribution<double> U(0.0, 1.0);
    uniform_int_distribution<int> Kdist(k_min, k_max);

    // destroy weights: guided, random, worst
    vector<double> destroy_weights = {10.0, 10.0, 10.0};
    // repair weights: greedy, randomized
    vector<double> repair_weights = {10.0, 10.0};

    const double decay = 0.85;

    for (int it = 0; it < iterations; it++) {
        vector<PlacedBay> cand = current;
        int k = Kdist(rng);

        discrete_distribution<int> d_dist(destroy_weights.begin(), destroy_weights.end());
        discrete_distribution<int> r_dist(repair_weights.begin(), repair_weights.end());
        int d_idx = d_dist(rng);
        int r_idx = r_dist(rng);

        // destroy
        if (d_idx == 0) destroy_guided(cand, obstacles, k, rng);
        else if (d_idx == 1) destroy_random(cand, k, rng);
        else destroy_worst(cand, k, rng);

        // repair
        if (r_idx == 0) {
            repair_greedy(cand, types, warehouse, obstacles, ceiling, wh_area,
                          k + repair_extra, rng, max_pts_add, angle_sample, intensify_bias);
        } else {
            repair_randomized(cand, types, warehouse, obstacles, ceiling, wh_area,
                              k + repair_extra, rng, max_pts_add, angle_sample, intensify_bias);
        }

        double q = quality(cand, wh_area);

        bool accept = false;
        double reward = 0.0;

        if (q < best_q) {
            accept = true;
            reward = 10.0;
            best = cand;
            best_q = q;
        } else if (q < current_q) {
            accept = true;
            reward = 5.0;
        } else {
            double delta = q - current_q;
            double p = exp(-delta / max(1e-12, T));
            if (U(rng) < p) {
                accept = true;
                reward = 2.0;
            } else {
                reward = 0.5;
            }
        }

        if (accept) {
            current = cand;
            current_q = q;
        }

        // update both selected operator weights
        destroy_weights[d_idx] = destroy_weights[d_idx]*decay + reward*(1.0-decay);
        repair_weights[r_idx]  = repair_weights[r_idx]*decay  + reward*(1.0-decay);

        T *= alpha;
    }

    return best;
}

vector<PlacedBay> alns(
    vector<PlacedBay> sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    mt19937& rng)
{
    return alns_core(sol, types, warehouse, obstacles, ceiling, wh_area, rng,
                     LNS_ITERATIONS, LNS_K_MIN, LNS_K_MAX, LNS_REPAIR_EXTRA,
                     ANGLE_SAMPLE, MAX_PTS_ADD, LNS_SA_T0, LNS_SA_ALPHA, false);
}

vector<PlacedBay> intensify(
    vector<PlacedBay> sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double,double>>& ceiling,
    double wh_area,
    mt19937& rng)
{
    return alns_core(sol, types, warehouse, obstacles, ceiling, wh_area, rng,
                     INTENSIFY_ITER, INTENSIFY_K_MIN, INTENSIFY_K_MAX, LNS_REPAIR_EXTRA + 1,
                     INTENSIFY_ANGLE_SAMPLE, INTENSIFY_MAX_PTS_ADD,
                     INTENSIFY_T0, INTENSIFY_ALPHA, true);
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

void compute_metrics(const vector<PlacedBay>& sol,
                     double& used_area,
                     long long& total_price,
                     long long& total_loads)
{
    used_area = 0.0;
    total_price = 0;
    total_loads = 0;
    for (const auto& p : sol) {
        used_area += (double)p.w * p.d;
        total_price += p.price;
        total_loads += p.loads;
    }
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

    if (warehouse.size() < 3 || types.empty()) {
        cerr << "Invalid input: warehouse polygon or bay types missing.\n";
        return 1;
    }

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
            sol = alns(sol,types,warehouse,obstacles,ceiling,wh_area,trng);

            double q = quality(sol,wh_area);
            bool valid = is_valid_solution(sol,warehouse,obstacles,ceiling);

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

    // Intensification phase on best global
    if (!best_global.empty()) {
        mt19937 irng = make_rng(999999u);
        auto improved = intensify(best_global, types, warehouse, obstacles, ceiling, wh_area, irng);
        double q2 = quality(improved, wh_area);
        bool valid2 = is_valid_solution(improved, warehouse, obstacles, ceiling);

        cout << "Intensification -> bays=" << improved.size()
             << " Q=" << fixed << setprecision(6) << q2
             << " valid=" << valid2 << "\n";

        if (valid2 && q2 < best_global_q) {
            best_global = improved;
            best_global_q = q2;
        }
    }

    if (!best_global.empty() && !is_valid_solution(best_global,warehouse,obstacles,ceiling))
        best_global.clear();

    ofstream out("solution.csv");
    out << "Id,X,Y,Rotation\n";
    for (auto& p : best_global)
        out << p.id << "," << llround(p.x) << "," << llround(p.y) << "," << p.angle << "\n";
    out.close();

    double used_area = 0.0;
    long long total_price = 0, total_loads = 0;
    compute_metrics(best_global, used_area, total_price, total_loads);

    cout << "\nsolution.csv written (" << best_global.size() << " bays)\n";
    cout << "Q = " << fixed << setprecision(6) << best_global_q << "\n";
    cout << "Used area = " << fixed << setprecision(2) << used_area << " mm²\n";
    cout << "Total price = " << total_price << "\n";
    cout << "Total loads = " << total_loads << "\n";
    if (wh_area > 0.0) {
        cout << "Area usage = " << fixed << setprecision(2)
             << (100.0 * used_area / wh_area) << "%\n";
    }

    return 0;
}