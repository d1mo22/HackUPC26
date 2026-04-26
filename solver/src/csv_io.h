#pragma once

#include "types.h"

vector<string> split_csv_line(string line);
bool file_empty_or_missing(const string& path);
bool try_stod(const string& s, double& out);
bool try_stoi(const string& s, int& out);
vector<Point> read_warehouse(const string& path);
vector<Obstacle> read_obstacles(const string& path);
vector<pair<double, double>> read_ceiling(const string& path);
vector<BayType> read_bays(const string& path);
