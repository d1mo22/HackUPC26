// geometry.cpp
#include "geometry.h"
using namespace std;

namespace {

void project_poly(const vector<Point>& poly, Point axis, double& mn, double& mx) {
    mn = mx = (poly[0].x * axis.x + poly[0].y * axis.y);
    for (int i = 1; i < (int)poly.size(); i++) {
        double v = poly[i].x * axis.x + poly[i].y * axis.y;
        mn = min(mn, v);
        mx = max(mx, v);
    }
}

bool bounds_disjoint(const Bounds& a, const Bounds& b) {
    return a.max_x <= b.min_x + EPS || b.max_x <= a.min_x + EPS ||
           a.max_y <= b.min_y + EPS || b.max_y <= a.min_y + EPS;
}

bool point_inside_or_on_polygon(Point p, const vector<Point>& poly) {
    int n = (int)poly.size();
    for (int i = 0; i < n; i++) {
        Point a = poly[i];
        Point b = poly[(i + 1) % n];
        // punto sobre el segmento
        if (fabs((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)) <= EPS &&
            min(a.x, b.x) - EPS <= p.x && p.x <= max(a.x, b.x) + EPS &&
            min(a.y, b.y) - EPS <= p.y && p.y <= max(a.y, b.y) + EPS) {
            return true;
        }
    }
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        Point a = poly[i];
        Point b = poly[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            double x_intersect = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
            if (p.x < x_intersect) inside = !inside;
        }
    }
    return inside;
}

bool proper_segment_intersection(Point a, Point b, Point c, Point d) {
    auto orient_local = [](Point p, Point q, Point r) -> int {
        double v = (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
        if (fabs(v) < EPS) return 0;
        return v > 0 ? 1 : -1;
    };
    int o1 = orient_local(a, b, c);
    int o2 = orient_local(a, b, d);
    int o3 = orient_local(c, d, a);
    int o4 = orient_local(c, d, b);
    return o1 * o2 < 0 && o3 * o4 < 0;
}

bool any_proper_segment_crossing(const vector<Point>& a, const vector<Point>& b) {
    for (int i = 0; i < (int)a.size(); i++) {
        Point a1 = a[i];
        Point a2 = a[(i + 1) % a.size()];
        for (int j = 0; j < (int)b.size(); j++) {
            Point b1 = b[j];
            Point b2 = b[(j + 1) % b.size()];
            if (proper_segment_intersection(a1, a2, b1, b2)) return true;
        }
    }
    return false;
}

bool sat_overlap_positive_area(const vector<Point>& a, const vector<Point>& b) {
    if (bounds_disjoint(Geometry::polygon_bounds(a), Geometry::polygon_bounds(b)))
        return false;

    auto separated_on_axes = [&](const vector<Point>& poly) -> bool {
        for (int i = 0; i < (int)poly.size(); i++) {
            Point p1   = poly[i];
            Point p2   = poly[(i + 1) % poly.size()];
            Point edge = {p2.x - p1.x, p2.y - p1.y};
            Point axis = {-edge.y, edge.x};
            double len = hypot(axis.x, axis.y);
            if (len < EPS) continue;
            axis.x /= len;
            axis.y /= len;
            double minA, maxA, minB, maxB;
            project_poly(a, axis, minA, maxA);
            project_poly(b, axis, minB, maxB);
            if (maxA <= minB + EPS || maxB <= minA + EPS) return true;
        }
        return false;
    };

    if (separated_on_axes(a)) return false;
    if (separated_on_axes(b)) return false;
    return true;
}

}

double Geometry::cross(Point a, Point b, Point c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

double Geometry::dotp(Point a, Point b) {
    return a.x * b.x + a.y * b.y;
}

double Geometry::polygon_area(const vector<Point>& p) {
    double a = 0;
    for (int i = 0; i < (int)p.size(); i++) {
        Point p1 = p[i];
        Point p2 = p[(i + 1) % p.size()];
        a += p1.x * p2.y - p2.x * p1.y;
    }
    return fabs(a) / 2.0;
}

bool Geometry::point_on_segment(Point p, Point a, Point b) {
    if (fabs(cross(a, b, p)) > EPS) return false;
    return min(a.x, b.x) - EPS <= p.x && p.x <= max(a.x, b.x) + EPS &&
           min(a.y, b.y) - EPS <= p.y && p.y <= max(a.y, b.y) + EPS;
}

int Geometry::orient(Point a, Point b, Point c) {
    double v = cross(a, b, c);
    if (fabs(v) < EPS) return 0;
    return v > 0 ? 1 : -1;
}

Bounds Geometry::polygon_bounds(const vector<Point>& poly) {
    Bounds b = {poly[0].x, poly[0].x, poly[0].y, poly[0].y};
    for (auto p : poly) {
        b.min_x = min(b.min_x, p.x);
        b.max_x = max(b.max_x, p.x);
        b.min_y = min(b.min_y, p.y);
        b.max_y = max(b.max_y, p.y);
    }
    return b;
}

bool Geometry::polygon_inside_polygon(const vector<Point>& small, const vector<Point>& big) {
    for (auto p : small) {
        if (!point_inside_or_on_polygon(p, big)) return false;
    }
    if (any_proper_segment_crossing(small, big)) return false;
    return true;
}

bool Geometry::polygons_overlap_area(const vector<Point>& a, const vector<Point>& b) {
    return sat_overlap_positive_area(a, b);
}

const Trig& Geometry::trig_for_angle(int angle) {
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

vector<Point> Geometry::make_rotated_rect(double x, double y, double w, double d, int angle) {
    const Trig& trig = trig_for_angle(angle);
    double ca = trig.cos_v;
    double sa = trig.sin_v;
    array<Point, 4> local = {{ {0,0}, {w,0}, {w,d}, {0,d} }};
    vector<Point> res;
    res.reserve(4);
    for (auto p : local) {
        res.push_back({x + p.x * ca - p.y * sa,
                       y + p.x * sa + p.y * ca});
    }
    return res;
}

vector<Point> Geometry::gap_poly_values(double x, double y, int w, int d, int gap, int angle) {
    if (gap <= 0) return {};
    const Trig& trig = trig_for_angle(angle);
    double ca = trig.cos_v;
    double sa = trig.sin_v;
    array<Point, 4> local = {{ {0,(double)d}, {(double)w,(double)d},
                                {(double)w,(double)d+gap}, {0,(double)d+gap} }};
    vector<Point> res;
    res.reserve(4);
    for (auto p : local) {
        res.push_back({x + p.x * ca - p.y * sa,
                       y + p.x * sa + p.y * ca});
    }
    return res;
}