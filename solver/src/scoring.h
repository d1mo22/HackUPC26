#pragma once
#include "geometry.h"

struct QSums { double price = 0, loads = 0, area = 0; };

double quality(const vector<PlacedBay>& sol, double warehouse_area);
QSums compute_sums(const vector<PlacedBay>& sol);
double quality_from_sums(const QSums& s, double warehouse_area, bool empty);
double quality_with_added(const QSums& cur, double price, double loads, double area, double warehouse_area);
double used_area(const vector<PlacedBay>& sol);
double obstacles_area(const vector<Obstacle>& obstacles);
double available_warehouse_area(const vector<Point>& warehouse, const vector<Obstacle>& obstacles);
tuple<double, int, int, double> details(const vector<PlacedBay>& sol, double wh_area);
double bay_score_mode(const BayType& t, int mode, double wh_area);
double candidate_score_mode(const BayType& t, double x, double y, int angle, double wh_area, int mode);
