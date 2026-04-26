#pragma once
#include <vector>
#include <array>
#include <cmath>
#include "types.h"


inline constexpr double EPS = 1e-7;
inline constexpr double PI  = 3.14159265358979323846;

struct Bounds { double min_x, max_x, min_y, max_y; };
struct Trig   { double cos_v, sin_v; };

namespace Geometry {
    double cross(Point a, Point b, Point c);
    double dotp(Point a, Point b);
    double polygon_area(const std::vector<Point>& p);
    bool   point_on_segment(Point p, Point a, Point b);
    int    orient(Point a, Point b, Point c);
    bool   polygon_inside_polygon(const std::vector<Point>& small, const std::vector<Point>& big);
    bool   polygons_overlap_area(const std::vector<Point>& a, const std::vector<Point>& b);
    Bounds polygon_bounds(const std::vector<Point>& poly);
    const Trig& trig_for_angle(int angle);
    std::vector<Point> make_rotated_rect(double x, double y, double w, double d, int angle);
    std::vector<Point> gap_poly_values(double x, double y, int w, int d, int gap, int angle);
}