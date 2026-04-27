#pragma once

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
#include <iomanip>

using namespace std;
using Clock = chrono::steady_clock;
namespace fs = std::filesystem;

const vector<string> PREFERRED_CASE_ORDER = {"CaseWeird", "Case1", "Case2", "Case3", "Case0", "Case40", "CaseAngledA", "CaseAngledB", "CaseAngledC", "CaseAngledD", "CaseDiagArmA", "CaseDiagArmB", "CaseDiagArmC", "CaseDiagArmD","CaseForcedAngle"};

const int ITERATIONS = 1500;          // legacy ceiling — actual stop is the deadline
const double CASE_BUDGET_SECONDS = 30.0;  // wall budget per restart; restarts run in parallel
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

extern thread_local mt19937 rng;

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

// Bundles the read-only context every operator needs.
// Replaces the 6-argument tail (types, warehouse, obstacles, ceiling, wh_area, mode).
struct OperatorContext {
    const vector<BayType>& types;
    const vector<Point>& warehouse;
    const vector<Obstacle>& obstacles;
    const vector<pair<double,double>>& ceiling;
    double wh_area;
    int mode;
};

extern vector<int> ANGLES;

inline bool is_orthogonal_angle(int angle) {
    int normalized = angle % 360;
    if (normalized < 0) normalized += 360;
    return normalized == 0 || normalized == 90 || normalized == 180 || normalized == 270;
}

inline bool is_diagonal_45_angle(int angle) {
    int normalized = angle % 360;
    if (normalized < 0) normalized += 360;
    return normalized % 45 == 0 && !is_orthogonal_angle(normalized);
}

inline bool is_allowed_angle(int angle) {
    int normalized = angle % 360;
    if (normalized < 0) normalized += 360;
    return normalized % 45 == 0;
}
