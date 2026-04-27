#include "geometry.h"

thread_local mt19937 rng(42);

vector<int> ANGLES = {
    0, 45, 90, 135, 180, 225, 270, 315
};

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

// Strict point-in-polygon test using ray casting only.
// Returns true iff p is in the interior (boundary points return false).
// Works for simple polygons; for non-simple/self-touching polygons it uses the
// even-odd rule, which correctly identifies "holes" formed by stitched outer/inner
// boundaries (as in warehouses with internal cutouts) as outside.
static bool point_strictly_inside_polygon(Point p, const vector<Point>& poly) {
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

bool polygon_inside_polygon(const vector<Point>& small, const vector<Point>& big) {
    for (auto p : small) {
        if (!point_inside_or_on_polygon(p, big)) {
            return false;
        }
    }

    if (any_proper_segment_crossing(small, big)) {
        return false;
    }

    // For non-simple polygons (warehouses with internal cutouts where the
    // outer and inner boundaries are stitched into a single vertex list),
    // the two checks above can be fooled: every corner of `small` may lie ON
    // a `big` edge while `small`'s interior is in a hole, with no proper edge
    // crossings. Defend against this by requiring the centroid of `small` to
    // be strictly inside `big`. For convex `small` (rotated rectangles), this
    // is sufficient when combined with the corner + crossing checks above.
    if (small.empty()) return true;

    Point centroid = {0, 0};
    for (auto p : small) {
        centroid.x += p.x;
        centroid.y += p.y;
    }
    centroid.x /= (double)small.size();
    centroid.y /= (double)small.size();

    if (!point_strictly_inside_polygon(centroid, big)) {
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

// ================= SpatialIndex =================

void SpatialIndex::insert_bb(int idx, double xmin, double ymin, double xmax, double ymax) {
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

void SpatialIndex::build(const vector<PlacedBay>& sol) {
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

void SpatialIndex::query(double xmin, double ymin, double xmax, double ymax, vector<int>& out) const {
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
    int ignore,
    const SpatialIndex* idx
) {
    vector<Point> candidate_bay = make_rotated_rect(x, y, w, d, angle);
    vector<Point> candidate_gap = gap_poly_values(x, y, w, d, gap, angle);

    Bounds cand_bb = polygon_bounds(candidate_bay);
    Bounds gap_bb = candidate_gap.empty() ? cand_bb : polygon_bounds(candidate_gap);

    if (!polygon_inside_polygon(candidate_bay, warehouse)) return false;

    {
        double min_h = min_ceiling_between(cand_bb.min_x, cand_bb.max_x, ceiling);
        if (h > min_h + EPS) return false;
    }

    if (!candidate_gap.empty()) {
        if (!polygon_inside_polygon(candidate_gap, warehouse)) return false;
    }

    for (auto& o : obstacles) {
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
        if (!bb_disjoint_v(cand_bb.min_x, cand_bb.min_y, cand_bb.max_x, cand_bb.max_y,
                           other.bay_min_x, other.bay_min_y, other.bay_max_x, other.bay_max_y)) {
            if (polygons_overlap_area(candidate_bay, other.bay_pts)) return false;
        }
        if (other.gap > 0 &&
            !bb_disjoint_v(cand_bb.min_x, cand_bb.min_y, cand_bb.max_x, cand_bb.max_y,
                           other.gap_min_x, other.gap_min_y, other.gap_max_x, other.gap_max_y)) {
            if (polygons_overlap_area(candidate_bay, other.gap_pts)) return false;
        }
        if (!candidate_gap.empty() &&
            !bb_disjoint_v(gap_bb.min_x, gap_bb.min_y, gap_bb.max_x, gap_bb.max_y,
                           other.bay_min_x, other.bay_min_y, other.bay_max_x, other.bay_max_y)) {
            if (polygons_overlap_area(candidate_gap, other.bay_pts)) return false;
        }
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
