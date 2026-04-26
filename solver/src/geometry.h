#pragma once
#include "types.h"

// ---- angle helpers ----
vector<int> prioritized_angles(const vector<int>& angles_source);

// ---- core geometry ----
double cross(Point a, Point b, Point c);
double dotp(Point a, Point b);
double polygon_area(const vector<Point>& p);
bool point_on_segment(Point p, Point a, Point b);
int orient(Point a, Point b, Point c);
bool proper_segment_intersection(Point a, Point b, Point c, Point d);
bool point_inside_or_on_polygon(Point p, const vector<Point>& poly);
bool any_proper_segment_crossing(const vector<Point>& a, const vector<Point>& b);
bool polygon_inside_polygon(const vector<Point>& small, const vector<Point>& big);
void project_poly(const vector<Point>& poly, Point axis, double& mn, double& mx);
Bounds polygon_bounds(const vector<Point>& poly);
bool bounds_disjoint(const Bounds& a, const Bounds& b);
bool sat_overlap_positive_area(const vector<Point>& a, const vector<Point>& b);
bool polygons_overlap_area(const vector<Point>& a, const vector<Point>& b);

// ---- trig / rotated rects ----
const Trig& trig_for_angle(int angle);
vector<Point> make_rotated_rect(double x, double y, double w, double d, int angle);
vector<Point> gap_poly_values(double x, double y, int w, int d, int gap, int angle);
const vector<Point>& bay_poly(const PlacedBay& p);
const vector<Point>& gap_poly(const PlacedBay& p);
void refresh_cache(PlacedBay& p);
vector<Point> obstacle_poly(const Obstacle& o);
pair<double, double> minmax_x(const vector<Point>& poly);

// ---- ceiling ----
double min_ceiling_between(double x1, double x2, const vector<pair<double, double>>& ceiling);

// ---- spatial index ----
struct SpatialIndex {
    double cell_size = 0;
    double origin_x = 0, origin_y = 0;
    int nx = 0, ny = 0;
    vector<vector<int>> cells;
    mutable vector<int> seen_gen;
    mutable int cur_gen = 0;

    void insert_bb(int idx, double xmin, double ymin, double xmax, double ymax);
    void build(const vector<PlacedBay>& sol);
    void query(double xmin, double ymin, double xmax, double ymax, vector<int>& out) const;
};

inline bool bb_disjoint_v(double a_min_x, double a_min_y, double a_max_x, double a_max_y,
                          double b_min_x, double b_min_y, double b_max_x, double b_max_y) {
    return a_max_x <= b_min_x + EPS || b_max_x <= a_min_x + EPS ||
           a_max_y <= b_min_y + EPS || b_max_y <= a_min_y + EPS;
}

bool valid_candidate(
    double x, double y, int w, int d, int h, int gap, int angle,
    const vector<PlacedBay>& sol,
    const vector<Point>& warehouse,
    const vector<Obstacle>& obstacles,
    const vector<pair<double, double>>& ceiling,
    int ignore = -1,
    const SpatialIndex* idx = nullptr
);
