#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>
#include <fstream>
#include <tuple>
#include <numeric>
#include <string>
#include <chrono>
#include <climits>
#include <map>
#include <array>
#include <thread>
#include <set>
#include <unordered_set>

using namespace std;
using Clock = chrono::steady_clock;

const vector<string> CASES = {"Case0", "Case1", "Case2", "Case3"};

const double EPS = 1e-7;
const double PI = acos(-1.0);

thread_local mt19937 rng;

void init_rng(int seed) {
    auto now = chrono::steady_clock::now().time_since_epoch().count();
    rng.seed(seed + now + hash<thread::id>{}(this_thread::get_id()));
}

double seconds_since(Clock::time_point start) {
    return chrono::duration<double>(Clock::now() - start).count();
}

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

vector<int> ANGLES = {0, 45, 90, 135, 180, 225, 270, 315};
vector<int> ORTHOGONAL = {0, 90, 180, 270};
vector<int> DIAGONAL = {45, 135, 225, 315};

// ================= GEOMETRY =================

double cross(Point a, Point b, Point c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

double dotp(Point a, Point b) { return a.x * b.x + a.y * b.y; }

double polygon_area(const vector<Point>& p) {
    double a = 0;
    for (int i = 0; i < (int)p.size(); i++) {
        a += p[i].x * p[(i+1)%p.size()].y - p[(i+1)%p.size()].x * p[i].y;
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
    return orient(a, b, c) * orient(a, b, d) < 0 && orient(c, d, a) * orient(c, d, b) < 0;
}

bool point_inside_or_on_polygon(Point p, const vector<Point>& poly) {
    for (int i = 0; i < (int)poly.size(); i++) {
        if (point_on_segment(p, poly[i], poly[(i+1)%poly.size()])) return true;
    }
    bool inside = false;
    for (int i = 0, j = poly.size()-1; i < (int)poly.size(); j = i++) {
        if ((poly[i].y > p.y) != (poly[j].y > p.y)) {
            double x_intersect = (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x;
            if (p.x < x_intersect) inside = !inside;
        }
    }
    return inside;
}

bool any_proper_segment_crossing(const vector<Point>& a, const vector<Point>& b) {
    for (int i = 0; i < (int)a.size(); i++)
        for (int j = 0; j < (int)b.size(); j++)
            if (proper_segment_intersection(a[i], a[(i+1)%a.size()], b[j], b[(j+1)%b.size()])) return true;
    return false;
}

bool polygon_inside_polygon(const vector<Point>& small, const vector<Point>& big) {
    for (auto p : small) if (!point_inside_or_on_polygon(p, big)) return false;
    return !any_proper_segment_crossing(small, big);
}

void project_poly(const vector<Point>& poly, Point axis, double& mn, double& mx) {
    mn = mx = dotp(poly[0], axis);
    for (int i = 1; i < (int)poly.size(); i++) {
        double v = dotp(poly[i], axis);
        mn = min(mn, v); mx = max(mx, v);
    }
}

Bounds polygon_bounds(const vector<Point>& poly) {
    Bounds b = {poly[0].x, poly[0].x, poly[0].y, poly[0].y};
    for (auto p : poly) {
        b.min_x = min(b.min_x, p.x); b.max_x = max(b.max_x, p.x);
        b.min_y = min(b.min_y, p.y); b.max_y = max(b.max_y, p.y);
    }
    return b;
}

bool bounds_disjoint(const Bounds& a, const Bounds& b) {
    return a.max_x <= b.min_x + EPS || b.max_x <= a.min_x + EPS ||
           a.max_y <= b.min_y + EPS || b.max_y <= a.min_y + EPS;
}

bool sat_overlap_positive_area(const vector<Point>& a, const vector<Point>& b) {
    if (bounds_disjoint(polygon_bounds(a), polygon_bounds(b))) return false;
    auto separated = [&](const vector<Point>& poly) {
        for (int i = 0; i < (int)poly.size(); i++) {
            Point edge = {poly[(i+1)%poly.size()].x - poly[i].x, poly[(i+1)%poly.size()].y - poly[i].y};
            Point axis = {-edge.y, edge.x};
            double len = hypot(axis.x, axis.y);
            if (len < EPS) continue;
            axis.x /= len; axis.y /= len;
            double minA, maxA, minB, maxB;
            project_poly(a, axis, minA, maxA);
            project_poly(b, axis, minB, maxB);
            if (maxA <= minB + EPS || maxB <= minA + EPS) return true;
        }
        return false;
    };
    return !separated(a) && !separated(b);
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
    const Trig& t = trig_for_angle(angle);
    return {
        {x, y},
        {x + w*t.cos_v, y + w*t.sin_v},
        {x + w*t.cos_v - d*t.sin_v, y + w*t.sin_v + d*t.cos_v},
        {x - d*t.sin_v, y + d*t.cos_v}
    };
}

vector<Point> gap_poly_values(double x, double y, int w, int d, int gap, int angle) {
    if (gap <= 0) return {};
    const Trig& t = trig_for_angle(angle);
    return {
        {x - d*t.sin_v, y + d*t.cos_v},
        {x + w*t.cos_v - d*t.sin_v, y + w*t.sin_v + d*t.cos_v},
        {x + w*t.cos_v - (d+gap)*t.sin_v, y + w*t.sin_v + (d+gap)*t.cos_v},
        {x - (d+gap)*t.sin_v, y + (d+gap)*t.cos_v}
    };
}

void refresh_cache(PlacedBay& p) {
    p.bay_pts = make_rotated_rect(p.x, p.y, p.w, p.d, p.angle);
    Bounds bb = polygon_bounds(p.bay_pts);
    p.bay_min_x = bb.min_x; p.bay_min_y = bb.min_y;
    p.bay_max_x = bb.max_x; p.bay_max_y = bb.max_y;
    if (p.gap > 0) {
        p.gap_pts = gap_poly_values(p.x, p.y, p.w, p.d, p.gap, p.angle);
        Bounds gb = polygon_bounds(p.gap_pts);
        p.gap_min_x = gb.min_x; p.gap_min_y = gb.min_y;
        p.gap_max_x = gb.max_x; p.gap_max_y = gb.max_y;
    } else {
        p.gap_pts.clear();
        p.gap_min_x = p.gap_min_y = p.gap_max_x = p.gap_max_y = 0;
    }
}

vector<Point> obstacle_poly(const Obstacle& o) {
    return {{o.x, o.y}, {o.x+o.w, o.y}, {o.x+o.w, o.y+o.d}, {o.x, o.y+o.d}};
}

pair<double, double> minmax_x(const vector<Point>& poly) {
    double mn = poly[0].x, mx = poly[0].x;
    for (auto p : poly) { mn = min(mn, p.x); mx = max(mx, p.x); }
    return {mn, mx};
}

// Direcciones para "deslizar" bays contra obstáculos/bordes
Point direction_vector(int angle, bool along_width) {
    const Trig& t = trig_for_angle(angle);
    if (along_width) return {t.cos_v, t.sin_v};  // dirección del ancho
    return {-t.sin_v, t.cos_v};  // dirección de la profundidad
}

// ================= CSV =================

vector<string> split_csv_line(string line) {
    vector<string> out;
    string cur;
    for (char c : line) {
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    for (auto& s : out) {
        while (!s.empty() && isspace(s.front())) s.erase(s.begin());
        while (!s.empty() && isspace(s.back())) s.pop_back();
    }
    return out;
}

bool try_stod(const string& s, double& out) {
    if (s.empty()) return false;
    try { size_t pos = 0; out = stod(s, &pos); return pos > 0; } catch (...) { return false; }
}

bool try_stoi(const string& s, int& out) {
    if (s.empty()) return false;
    try { size_t pos = 0; out = stoi(s, &pos); return pos > 0; } catch (...) { return false; }
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
    ifstream f(path);
    if (!f.good()) return res;
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() < 4) continue;
        double a, b, c, d;
        if (!try_stod(v[0], a) || !try_stod(v[1], b) || !try_stod(v[2], c) || !try_stod(v[3], d)) continue;
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
        double seg_x1 = ceiling[i].first, h = ceiling[i].second;
        double seg_x2 = (i+1 < (int)ceiling.size()) ? ceiling[i+1].first : 1e18;
        double left = max(x1, seg_x1), right = min(x2, seg_x2);
        if (left < right + EPS) ans = min(ans, h);
    }
    return ans;
}

bool valid_candidate(double x, double y, int w, int d, int h, int gap, int angle,
    const vector<PlacedBay>& sol, const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles, const vector<pair<double, double>>& ceiling,
    int ignore = -1) {
    
    vector<Point> cb = make_rotated_rect(x, y, w, d, angle);
    vector<Point> cg = gap_poly_values(x, y, w, d, gap, angle);

    if (!polygon_inside_polygon(cb, warehouse)) return false;
    if (!cg.empty() && !polygon_inside_polygon(cg, warehouse)) return false;

    for (auto& o : obstacles) {
        auto op = obstacle_poly(o);
        if (polygons_overlap_area(cb, op)) return false;
        if (!cg.empty() && polygons_overlap_area(cg, op)) return false;
    }

    for (int i = 0; i < (int)sol.size(); i++) {
        if (i == ignore) continue;
        if (polygons_overlap_area(cb, sol[i].bay_pts)) return false;
        if (!cg.empty() && polygons_overlap_area(cg, sol[i].bay_pts)) return false;
        if (sol[i].gap > 0 && polygons_overlap_area(cb, sol[i].gap_pts)) return false;
        // Permitir gap-gap overlap
    }

    auto bx = minmax_x(cb);
    if (h > min_ceiling_between(bx.first, bx.second, ceiling) + EPS) return false;
    return true;
}

// ================= SCORE =================

/*double quality(const vector<PlacedBay>& sol, double warehouse_area) {
    if (sol.empty()) return 1e100;
    double tp = 0, tl = 0, ta = 0;
    for (auto& p : sol) { tp += p.price; tl += p.loads; ta += (double)p.w * p.d; }
    return pow(tp / max(1.0, tl), 2.0 - ta / warehouse_area);
}*/

double quality(const vector<PlacedBay>& sol, double warehouse_area) {
    if (sol.empty()) return 1e100;
    double tp = 0, tl = 0, ta = 0;
    for (auto& p : sol) { 
        tp += p.price; 
        tl += p.loads; 
        ta += (double)p.w * p.d; 
    }
    // Q = (precio / cargas) ^ (2 - área_ocupada / área_total_utilizable)
    double base = tp / max(1.0, tl);
    double exponent = 2.0 - (ta / warehouse_area);
    return pow(base, exponent);
}

double available_warehouse_area(const vector<Point>& warehouse, const vector<Obstacle>& obstacles) {
    double total_area = polygon_area(warehouse);
    double obs_area = 0;
    for (auto& o : obstacles) {
        // El obstáculo es un rectángulo axis-aligned
        obs_area += o.w * o.d;
        
        // Pero puede estar parcialmente fuera del warehouse,
        // así que calculamos la intersección
        auto op = obstacle_poly(o);
        // Solo contar si está dentro del warehouse
        bool inside = true;
        for (auto& p : op) {
            if (!point_inside_or_on_polygon(p, warehouse)) {
                inside = false;
                break;
            }
        }
        if (inside) {
            obs_area += o.w * o.d;
        } else {
            // Calcular área de intersección (aproximado)
            // ... 
        }
    }
    return max(1.0, total_area - obs_area);
}

// ================= HEURÍSTICA DE ORDENACIÓN =================

// La clave: evaluar cada bay NO solo por sus atributos individuales, sino por
// cómo "encaja" en el espacio restante. Para eso simulamos el impacto en Q.
struct ScoredBay {
    int type_idx;
    double score;
};

vector<ScoredBay> rank_bays_for_region(const vector<BayType>& types, double avail_area, double wh_area) {
    vector<ScoredBay> ranked;
    for (int i = 0; i < (int)types.size(); i++) {
        const BayType& t = types[i];
        double area = (double)t.w * t.d;
        double occupied = (double)t.w * (t.d + t.gap);
        
        // Eficiencia pura: cargas por precio
        double efficiency = (double)t.loads / max(1.0, (double)t.price);
        
        // Densidad: área útil vs área ocupada (incluyendo gap)
        double density = area / max(1.0, occupied);
        
        // Escalabilidad: qué tan bien llena el espacio disponible
        double fill_factor = min(1.0, area / max(1.0, avail_area));
        
        // Penalización por gap: más agresiva cuando el espacio es escaso
        double scarcity = 1.0 - min(1.0, avail_area / wh_area);  // 0 = mucho espacio, 1 = sin espacio
        double gap_ratio = (double)t.gap / max(1.0, (double)t.d);
        double gap_penalty = 1.0 / (1.0 + gap_ratio * (1.0 + scarcity * 3.0));
        
        // Bonus para bays sin gap en espacios reducidos
        double no_gap_bonus = (t.gap == 0 && scarcity > 0.5) ? 1.5 : 1.0;
        
        // Score combinado
        double score = efficiency * (1.0 + density) * (0.5 + 0.5 * fill_factor) * gap_penalty * no_gap_bonus;
        
        ranked.push_back({i, score});
    }
    
    sort(ranked.begin(), ranked.end(), [](const ScoredBay& a, const ScoredBay& b) {
        return a.score > b.score;
    });
    
    return ranked;
}
// ================= GREEDY CON BACKTRACKING LOCAL =================

PlacedBay make_bay(const BayType& t, double x, double y, int angle) {
    PlacedBay p;
    p.id = t.id; p.x = x; p.y = y;
    p.w = t.w; p.d = t.d; p.h = t.h;
    p.gap = t.gap; p.angle = angle;
    p.price = t.price; p.loads = t.loads;
    refresh_cache(p);
    return p;
}

// Desliza un bay en una dirección hasta tocar algo
bool slide_to_contact(PlacedBay& bay, Point dir, const vector<PlacedBay>& sol,
                      const vector<Point>& warehouse, const vector<Obstacle>& obstacles,
                      const vector<pair<double, double>>& ceiling, int ignore_idx) {
    double lo = 0, hi = 50000;
    
    // Búsqueda binaria para encontrar la distancia máxima
    for (int iter = 0; iter < 30; iter++) {
        double mid = (lo + hi) / 2;
        PlacedBay test = bay;
        test.x = bay.x + dir.x * mid;
        test.y = bay.y + dir.y * mid;
        refresh_cache(test);
        
        if (valid_candidate(test.x, test.y, test.w, test.d, test.h, test.gap, test.angle,
                           sol, warehouse, obstacles, ceiling, ignore_idx)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    
    if (lo > 1.0) {
        bay.x += dir.x * lo;
        bay.y += dir.y * lo;
        refresh_cache(bay);
        return true;
    }
    return false;
}

// Intenta colocar un bay y deslizarlo contra obstáculos/bordes para compactar
bool place_and_compact(PlacedBay& bay, const vector<PlacedBay>& sol,
                       const vector<Point>& warehouse, const vector<Obstacle>& obstacles,
                       const vector<pair<double, double>>& ceiling, int ignore_idx) {
    // Direcciones de deslizamiento: izquierda, abajo, y las diagonales
    vector<Point> dirs = {
        {-1, 0}, {0, -1}, {-1, -1}, {1, -1}
    };
    
    bool moved = false;
    for (auto dir : dirs) {
        if (slide_to_contact(bay, dir, sol, warehouse, obstacles, ceiling, ignore_idx)) {
            moved = true;
        }
    }
    return moved;
}

// Genera puntos candidatos cerca de bays existentes (para empaquetar pegado)
vector<Point> proximity_points(const vector<PlacedBay>& sol, const vector<Point>& warehouse) {
    vector<Point> pts;
    if (sol.empty()) return pts;
    
    for (auto& p : sol) {
        auto& bp = p.bay_pts;
        // Para cada arista del bay, generar puntos justo al lado
        for (int i = 0; i < (int)bp.size(); i++) {
            Point a = bp[i], b = bp[(i+1)%bp.size()];
            
            // Dirección normal hacia afuera (perpendicular a la arista, apuntando "afuera")
            double edge_x = b.x - a.x, edge_y = b.y - a.y;
            double len = hypot(edge_x, edge_y);
            if (len < 1) continue;
            
            // Normales (dos direcciones)
            double nx1 = -edge_y / len, ny1 = edge_x / len;
            double nx2 = edge_y / len, ny2 = -edge_x / len;
            
            // Puntos a lo largo de la arista, desplazados hacia afuera
            for (double t = 0; t <= 1.0; t += 0.2) {
                double base_x = a.x + t * edge_x;
                double base_y = a.y + t * edge_y;
                
                for (double offset : {100.0, 500.0, 1000.0}) {
                    Point p1 = {base_x + nx1 * offset, base_y + ny1 * offset};
                    Point p2 = {base_x + nx2 * offset, base_y + ny2 * offset};
                    if (point_inside_or_on_polygon(p1, warehouse)) pts.push_back(p1);
                    if (point_inside_or_on_polygon(p2, warehouse)) pts.push_back(p2);
                }
            }
        }
    }
    return pts;
}

// Greedy con backtracking local: prueba múltiples opciones y elige la que
// deja el mejor "futuro potencial"
struct Placement {
    double x, y;
    int type_idx;
    int angle;
    double immediate_score;
    double future_potential;  // estimación de cuánto espacio utilizable queda
};

double estimate_remaining_potential(const vector<PlacedBay>& sol, double wh_area) {
    double used = 0;
    for (auto& p : sol) used += (double)p.w * p.d;
    double remaining = wh_area - used;
    
    // Castigar soluciones que dejan espacios muy fragmentados
    // (aproximado: si hay pocos bays, el espacio restante probablemente sea más utilizable)
    double fragmentation_penalty = 1.0 / (1.0 + sol.size() * 0.01);
    
    return remaining * fragmentation_penalty;
}

bool smart_greedy_step(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area
) {
    vector<Point> candidates;
    
    for (auto p : warehouse) candidates.push_back(p);
    
    auto prox = proximity_points(sol, warehouse);
    candidates.insert(candidates.end(), prox.begin(), prox.end());
    
    // Añadir puntos en gaps de bays existentes para shared-gap
    for (auto& p : sol) {
        if (p.gap > 0 && !p.gap_pts.empty()) {
            // El centro del gap es un punto candidato perfecto para otro bay enfrentado
            double cx = 0, cy = 0;
            for (auto& gp : p.gap_pts) { cx += gp.x; cy += gp.y; }
            cx /= p.gap_pts.size(); cy /= p.gap_pts.size();
            if (point_inside_or_on_polygon({cx, cy}, warehouse)) {
                candidates.push_back({cx, cy});
            }
            // También los bordes del gap
            for (auto& gp : p.gap_pts) candidates.push_back(gp);
        }
    }
    
    for (auto& o : obstacles) {
        candidates.push_back({o.x, o.y}); candidates.push_back({o.x+o.w, o.y});
        candidates.push_back({o.x, o.y+o.d}); candidates.push_back({o.x+o.w, o.y+o.d});
    }
    
    if (sol.empty()) {
        Bounds bb = polygon_bounds(warehouse);
        for (double x = bb.min_x; x <= bb.max_x; x += 2000) {
            for (double y = bb.min_y; y <= bb.max_y; y += 2000) {
                if (point_inside_or_on_polygon({x, y}, warehouse)) {
                    // Verificar obstáculos
                    bool in_obs = false;
                    for (auto& o : obstacles) {
                        if (x >= o.x-EPS && x <= o.x+o.w+EPS && y >= o.y-EPS && y <= o.y+o.d+EPS) {
                            in_obs = true; break;
                        }
                    }
                    if (!in_obs) candidates.push_back({x, y});
                }
            }
        }
    }
    
    if (candidates.size() > 500) {
        shuffle(candidates.begin(), candidates.end(), rng);
        candidates.resize(500);
    }
    
    double cur_q = quality(sol, wh_area);
    vector<Placement> valid_placements;
    
    for (auto& pt : candidates) {
        double region_avail = wh_area;
        auto ranked = rank_bays_for_region(types, region_avail, wh_area);
        
        int types_to_try = min(8, (int)ranked.size());
        for (int ri = 0; ri < types_to_try; ri++) {
            int ti = ranked[ri].type_idx;
            const BayType& t = types[ti];
            
            // SIEMPRE probar todos los ángulos
            for (int angle : ANGLES) {
                if (!valid_candidate(pt.x, pt.y, t.w, t.d, t.h, t.gap, angle,
                                    sol, warehouse, obstacles, ceiling)) continue;
                
                vector<PlacedBay> tmp = sol;
                PlacedBay new_bay = make_bay(t, pt.x, pt.y, angle);
                int idx = tmp.size();
                tmp.push_back(new_bay);
                place_and_compact(tmp.back(), sol, warehouse, obstacles, ceiling, idx);
                
                double new_q = quality(tmp, wh_area);
                double immediate = cur_q - new_q;
                double future = estimate_remaining_potential(tmp, wh_area);
                
                Placement pl;
                pl.x = tmp.back().x; pl.y = tmp.back().y;
                pl.type_idx = ti;
                pl.angle = tmp.back().angle;
                pl.immediate_score = immediate;
                pl.future_potential = future;
                
                valid_placements.push_back(pl);
            }
        }
    }
    
    if (valid_placements.empty()) return false;
    /*
    sort(valid_placements.begin(), valid_placements.end(), [&](const Placement& a, const Placement& b) {
        bool a_improves = a.immediate_score > 0;
        bool b_improves = b.immediate_score > 0;
        if (a_improves != b_improves) return a_improves > b_improves;
        if (a_improves) return a.immediate_score > b.immediate_score;
        
        double a_combined = a.immediate_score * 0.7 + a.future_potential * 0.3 / wh_area;
        double b_combined = b.immediate_score * 0.7 + b.future_potential * 0.3 / wh_area;
        return a_combined > b_combined;
    });*/
    sort(valid_placements.begin(), valid_placements.end(), [&](const Placement& a, const Placement& b) {
    // SIEMPRE priorizar el que tenga mejor combined score,
    // incluso si empeora Q al principio
    double a_combined = a.immediate_score + a.future_potential * 0.3 / wh_area;
    double b_combined = b.immediate_score + b.future_potential * 0.3 / wh_area;
    return a_combined > b_combined;
    });
    
    auto& best = valid_placements[0];
    sol.push_back(make_bay(types[best.type_idx], best.x, best.y, best.angle));
    place_and_compact(sol.back(), sol, warehouse, obstacles, ceiling, sol.size()-1);
    
    return true;
}

// Helper: área usada
double used_area(const vector<PlacedBay>& sol) {
    double a = 0;
    for (auto& p : sol) a += (double)p.w * p.d;
    return a;
}

// Intenta colocar bays que comparten gap con bays existentes
bool add_shared_gap_bays(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area
) {
    if (sol.empty()) return false;
    
    double cur_q = quality(sol, wh_area);
    bool added = false;
    
    // Para cada bay existente con gap
    for (int i = 0; i < (int)sol.size(); i++) {
        if (sol[i].gap <= 0) continue;
        
        PlacedBay& anchor = sol[i];
        int opposite_angle = (anchor.angle + 180) % 360;
        
        // Vector en dirección de la profundidad + gap
        Point depth_dir = direction_vector(anchor.angle, false);  // dirección de profundidad
        
        // El nuevo bay debería colocarse al final del gap del ancla
        double offset_x = depth_dir.x * (anchor.d + anchor.gap);
        double offset_y = depth_dir.y * (anchor.d + anchor.gap);
        
        // Punto base: esquina del gap del ancla
        double base_x = anchor.x + offset_x;
        double base_y = anchor.y + offset_y;
        
        // Rankear bays para esta región
        auto ranked = rank_bays_for_region(types, wh_area - used_area(sol), wh_area);
        
        for (int ri = 0; ri < min(5, (int)ranked.size()); ri++) {
            const BayType& t = types[ranked[ri].type_idx];
            
            // Probar colocación enfrentada
            if (valid_candidate(base_x, base_y, t.w, t.d, t.h, t.gap, opposite_angle,
                               sol, warehouse, obstacles, ceiling, i)) {
                
                vector<PlacedBay> tmp = sol;
                tmp.push_back(make_bay(t, base_x, base_y, opposite_angle));
                
                double new_q = quality(tmp, wh_area);
                if (new_q < cur_q || sol.size() < 10) { 
                    sol = tmp;
                    cur_q = new_q;
                    added = true;
                    break;  // Salir después de una mejora
                }
            }
            
            // También probar desplazado lateralmente (compartir gap parcialmente)
            Point width_dir = direction_vector(anchor.angle, true);
            for (double lateral : {0.0, (double)t.w, -(double)t.w, (double)anchor.w, -(double)anchor.w}) {
                double try_x = base_x + width_dir.x * lateral;
                double try_y = base_y + width_dir.y * lateral;
                
                if (valid_candidate(try_x, try_y, t.w, t.d, t.h, t.gap, opposite_angle,
                                   sol, warehouse, obstacles, ceiling, i)) {
                    
                    vector<PlacedBay> tmp = sol;
                    tmp.push_back(make_bay(t, try_x, try_y, opposite_angle));
                    
                    double new_q = quality(tmp, wh_area);
                    if (new_q < cur_q) {
                        sol = tmp;
                        cur_q = new_q;
                        added = true;
                        break;
                    }
                }
            }
            
            if (added) break;
        }
        
        if (added) break;  // Una mejora por llamada es suficiente
    }
    
    return added;
}

void print_area_info(const vector<Point>& warehouse, const vector<Obstacle>& obstacles) {
    double total = polygon_area(warehouse);
    double obs = 0;
    for (auto& o : obstacles) obs += o.w * o.d;
    cout << "  Total warehouse area: " << total << "\n";
    cout << "  Obstacles area: " << obs << "\n";
    cout << "  Available area: " << total - obs << "\n";
    cout << "  Obstacles count: " << obstacles.size() << "\n";
    for (int i = 0; i < (int)obstacles.size(); i++) {
        cout << "    Obs" << i << ": (" << obstacles[i].x << "," << obstacles[i].y 
             << ") " << obstacles[i].w << "x" << obstacles[i].d 
             << " area=" << obstacles[i].w * obstacles[i].d << "\n";
    }
}


// Refinamiento: intenta recolocar bays existentes
bool try_reposition(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area
) {
    if (sol.size() < 2) return false;
    
    double best_q = quality(sol, wh_area);
    
    for (int i = 0; i < (int)sol.size(); i++) {
        vector<PlacedBay> tmp = sol;
        tmp.erase(tmp.begin() + i);
        double base_q = quality(tmp, wh_area);
        
        // Intentar encontrar una mejor posición para este bay (o uno mejor)
        bool found_better = false;
        
        for (auto& t : types) {
            // Probar en posiciones cercanas a la original
            for (double dx = -2000; dx <= 2000; dx += 500) {
                for (double dy = -2000; dy <= 2000; dy += 500) {
                    double nx = sol[i].x + dx, ny = sol[i].y + dy;
                    
                    for (int angle : ORTHOGONAL) {
                        if (!valid_candidate(nx, ny, t.w, t.d, t.h, t.gap, angle,
                                            tmp, warehouse, obstacles, ceiling)) continue;
                        
                        vector<PlacedBay> cand = tmp;
                        cand.push_back(make_bay(t, nx, ny, angle));
                        
                        double q = quality(cand, wh_area);
                        if (q < best_q) {
                            sol = cand;
                            best_q = q;
                            found_better = true;
                        }
                    }
                }
            }
        }
        
        if (found_better) return true;
    }
    
    return false;
}

// Estructura para sums acumuladas
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

double bay_score_mode(const BayType& t, int mode, double wh_area) {
    double area = (double)t.w * t.d;
    double reserved = (double)t.w * (t.d + t.gap);
    double price_load = (double)t.price / max(1.0, (double)t.loads);
    double area_ratio = area / max(1.0, wh_area);
    double gap_ratio = (reserved - area) / max(1.0, reserved);

    if (mode == 0) return -price_load;           // CHEAP_LOAD
    if (mode == 1) return area_ratio;             // BIG_AREA
    if (mode == 2) return -gap_ratio + 0.2 * area_ratio;  // LOW_GAP
    return 2.0 * area_ratio - 1.0 * price_load - 0.5 * gap_ratio; // BALANCED
}

Point angle_width_axis(int angle) {
    const Trig& trig = trig_for_angle(angle);
    return {trig.cos_v, trig.sin_v};
}

Point angle_depth_axis(int angle) {
    const Trig& trig = trig_for_angle(angle);
    return {-trig.sin_v, trig.cos_v};
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

    if ((int)anchors.size() > 24) {
        anchors.resize(24);
    }

    vector<BayType> sorted_types = types;
    sort(sorted_types.begin(), sorted_types.end(), [&](const BayType& a, const BayType& b) {
        return bay_score_mode(a, mode, wh_area) > bay_score_mode(b, mode, wh_area);
    });

    PlacedBay best;
    bool found = false;
    double best_q = 1e100;

    QSums cur = compute_sums(sol);

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

                if (!valid_candidate(x, y, t.w, t.d, t.h, t.gap, angle,
                                    sol, warehouse, obstacles, ceiling)) {
                    continue;
                }

                double q = quality_with_added(cur, t.price, t.loads, (double)t.w * t.d, wh_area);
                if (q < best_q) {
                    best_q = q;
                    best = make_bay(t, x, y, angle);
                    found = true;
                }
            }
        }
    }

    if (!found) return false;
    sol.push_back(best);
    return true;
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

    for (auto& t : types) {
        for (int angle : ANGLES) {
            if (!valid_candidate(old.x, old.y, t.w, t.d, t.h, t.gap, angle,
                                sol, warehouse, obstacles, ceiling)) continue;

            double q = quality_with_added(cur, t.price, t.loads, (double)t.w * t.d, wh_area);
            if (q < best_q) {
                best_q = q;
                best = make_bay(t, old.x, old.y, angle);
                found = true;
            }
        }
    }

    sol.push_back(found ? best : old);
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

    static const vector<int> shared_angles = {0, 180, 90, 270, 45, 225, 135, 315};

    for (int i = 0; i < k + 5; i++) {
        if (!add_shared_gap_bay(sol, types, warehouse, obstacles, ceiling, wh_area, mode)) {
            // Fallback: add_bay normal con ángulos que favorecen shared-gap
            // (usamos tu smart_greedy_step que ya prueba todos los ángulos)
            if (!smart_greedy_step(sol, types, warehouse, obstacles, ceiling, wh_area)) break;
        }
    }

    if (quality(sol, wh_area) > quality(backup, wh_area)) {
        sol = backup;
    }
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

bool is_orthogonal_angle(int angle) {
    int normalized = angle % 360;
    if (normalized < 0) normalized += 360;
    return normalized == 0 || normalized == 90 || normalized == 180 || normalized == 270;
}

bool is_allowed_angle(int angle) {
    int normalized = angle % 360;
    if (normalized < 0) normalized += 360;
    return normalized % 45 == 0;
}

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
        pts.push_back({o.x + o.w / 2.0, o.y});
        pts.push_back({o.x + o.w / 2.0, o.y + o.d});
        pts.push_back({o.x, o.y + o.d / 2.0});
        pts.push_back({o.x + o.w, o.y + o.d / 2.0});
    }

    for (auto& p : sol) {
        auto& bp = p.bay_pts;
        auto& gp = p.gap_pts;
        for (auto q : bp) pts.push_back({round(q.x), round(q.y)});
        for (auto q : gp) pts.push_back({round(q.x), round(q.y)});
        if ((int)bp.size() == 4) {
            for (int i = 0; i < 4; i++) {
                Point a = bp[i], b = bp[(i + 1) % 4];
                pts.push_back({round((a.x + b.x) / 2.0), round((a.y + b.y) / 2.0)});
            }
        }
    }

    shuffle(pts.begin(), pts.end(), rng);
    if ((int)pts.size() > 500) pts.resize(500);
    return pts;
}

vector<int> prioritized_angles(const vector<int>& angles_source) {
    vector<int> orthogonal, fallback;
    for (int angle : angles_source) {
        if (!is_allowed_angle(angle)) continue;
        if (is_orthogonal_angle(angle)) orthogonal.push_back(angle);
        else fallback.push_back(angle);
    }
    shuffle(orthogonal.begin(), orthogonal.end(), rng);
    shuffle(fallback.begin(), fallback.end(), rng);
    orthogonal.insert(orthogonal.end(), fallback.begin(), fallback.end());
    return orthogonal;
}

PlacedBay make_candidate(const BayType& t, double x, double y, int angle) {
    PlacedBay p;
    p.id = t.id; p.x = x; p.y = y;
    p.w = t.w; p.d = t.d; p.h = t.h;
    p.gap = t.gap; p.angle = angle;
    p.price = t.price; p.loads = t.loads;
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
    int point_limit = min(80, (int)pts.size());

    for (int pi = 0; pi < point_limit; pi++) {
        double x = pts[pi].x, y = pts[pi].y;
        for (auto& t : sorted_types) {
            vector<int> angles = prioritized_angles(ANGLES);
            int limit = min(14, (int)angles.size());
            for (int ai = 0; ai < limit; ai++) {
                int angle = angles[ai];
                if (!valid_candidate(x, y, t.w, t.d, t.h, t.gap, angle,
                                    sol, warehouse, obstacles, ceiling)) continue;

                double s = bay_score_mode(t, mode, wh_area);
                if (is_orthogonal_angle(angle)) s += 0.05;
                s -= 0.000001 * (x + y);

                if (s > best_s) {
                    best_s = s;
                    best = make_candidate(t, x, y, angle);
                    found = true;
                }
            }
        }
    }

    if (found) { sol.push_back(best); return true; }
    return false;
}

void shelf_pack_into(
    vector<PlacedBay>& sol,
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area,
    int mode,
    int orientation,
    double off_x,
    double off_y
) {
    Bounds wh_bb = polygon_bounds(warehouse);

    vector<BayType> sorted = types;
    sort(sorted.begin(), sorted.end(), [&](const BayType& a, const BayType& b) {
        return bay_score_mode(a, mode, wh_area) > bay_score_mode(b, mode, wh_area);
    });

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

    while (y < wh_bb.max_y && rows < 500) {
        rows++;
        double x = wh_bb.min_x + off_x;
        double max_row_dim = 0.0;
        bool placed_any = false;
        int safety = 0;

        while (x < wh_bb.max_x && safety++ < 1000) {
            bool placed = false;
            for (auto& t : sorted) {
                double ax, ay; int angle;
                if (try_place(x, y, t, orientation, ax, ay, angle) ||
                    try_place(x, y, t, 1 - orientation, ax, ay, angle)) {
                    sol.push_back(make_candidate(t, ax, ay, angle));
                    x += (angle == 0 || angle == 180) ? t.w : (double)t.d + t.gap;
                    max_row_dim = max(max_row_dim, (angle == 0 || angle == 180) ? (double)t.d + t.gap : (double)t.w);
                    placed = true; placed_any = true;
                    break;
                }
            }
            if (!placed) x += 100.0;
        }
        if (!placed_any) y += 200.0;
        else y += max_row_dim;
    }
}

vector<PlacedBay> build_optimized_greedy(
    const vector<BayType>& types,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    double wh_area
) {
    vector<PlacedBay> sol;
    
    // Fase 1: shelf_pack para axis-aligned, o add_bay para no axis-aligned
    bool axis_aligned = warehouse_is_axis_aligned(warehouse);
    
    if (axis_aligned) {
        // Usar shelf_pack_into del código original (probado)
        int min_w = INT_MAX;
        for (auto& t : types) min_w = min(min_w, t.w);
        double off = min_w / 2.0;
        
        double best_q = 1e100;
        for (int orient = 0; orient < 2; orient++) {
            for (double ox : {0.0, off}) {
                for (double oy : {0.0, off}) {
                    vector<PlacedBay> tmp;
                    shelf_pack_into(tmp, types, warehouse, obstacles, ceiling, wh_area, 3, orient, ox, oy);
                    double q = quality(tmp, wh_area);
                    if (q < best_q) { best_q = q; sol = tmp; }
                }
            }
        }
    }
    
    // Fase 2: add_bay y add_shared_gap_bay (del código original)
    for (int i = 0; i < 200; i++) {
        bool added = false;
        
        // Intentar shared-gap primero
        if (!sol.empty()) {
            added = add_shared_gap_bay(sol, types, warehouse, obstacles, ceiling, wh_area, 3);
        }
        
        // Si no, add_bay normal
        if (!added) {
            if (!add_bay(sol, types, warehouse, obstacles, ceiling, wh_area, 3)) break;
        }
        
        if (i % 25 == 0) {
            cout << "  Step " << i+1 << ": " << sol.size() << " bays, Q = " << quality(sol, wh_area) << "\n";
        }
    }
    
    // Fase 3: Refinamiento ligero (usando operadores del original)
    double prev_q = quality(sol, wh_area);
    for (int iter = 0; iter < 5; iter++) {
        // upgrade_bay
        if (!sol.empty()) {
            upgrade_bay(sol, types, warehouse, obstacles, ceiling, wh_area, 3);
        }
        
        // shared_gap_refill (puede quitar y poner bays)
        shared_gap_refill(sol, types, warehouse, obstacles, ceiling, wh_area, 3);
        
        double cur_q = quality(sol, wh_area);
        if (fabs(cur_q - prev_q) < 0.01) break;
        prev_q = cur_q;
    }
    
    return sol;
}

// ================= MAIN =================

void solve_case(const string& case_dir) {
    auto start = Clock::now();
    cout << "\n=== OPTIMIZED GREEDY - " << case_dir << " ===\n";
    
    auto warehouse = read_warehouse(case_dir + "/warehouse.csv");
    auto obstacles = read_obstacles(case_dir + "/obstacles.csv");
    auto ceiling = read_ceiling(case_dir + "/ceiling.csv");
    auto types = read_bays(case_dir + "/types_of_bays.csv");
    
    double wh_area = available_warehouse_area(warehouse, obstacles);
    cout << "Available area: " << wh_area << " | Types: " << types.size() << "\n\n";
    
    init_rng(42);
    auto sol = build_optimized_greedy(types, warehouse, obstacles, ceiling, wh_area);
    
    double area = 0, loads = 0, price = 0;
    for (auto& p : sol) {
        area += (double)p.w * p.d;
        loads += p.loads;
        price += p.price;
    }
    double q = quality(sol, wh_area);
    
    cout << "\n--- Final Result ---\n";
    cout << "Bays: " << sol.size() << "\n";
    cout << "Area: " << area << " (" << (area/wh_area*100) << "%)\n";
    cout << "Loads: " << loads << "\n";
    cout << "Price: " << price << "\n";
    cout << "Quality: " << q << "\n";
    cout << "Time: " << seconds_since(start) << "s\n";
    
    ofstream out(case_dir + "/solution_greedy.csv");
    out << "Id,X,Y,Rotation\n";
    for (auto& p : sol) {
        out << p.id << "," << llround(p.x) << "," << llround(p.y) << "," << p.angle << "\n";
    }
    cout << "Saved: " << case_dir + "/solution_greedy.csv" << "\n";

    print_area_info(warehouse, obstacles);
}

int main(int argc, char* argv[]) {
    auto start = Clock::now();
    if (argc >= 2) {
        // CLI mode: solve a single case directory
        solve_case(argv[1]);
    } else {
        // Discovery mode: solve known cases
        for (auto& c : CASES) {
            ifstream f(c + "/warehouse.csv");
            if (f.good()) solve_case(c);
        }
        cout << "\nTotal time: " << seconds_since(start) << "s\n";
    }
    return 0;
}