// warehouse_io.cpp
#include "warehouse_io.h"
#include <fstream>
#include <algorithm>
using namespace std;

vector<string> WarehouseIO::split_csv_line(const string& line) {
    vector<string> out;
    string cur;
    for (char c : line) {
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else            cur.push_back(c);
    }
    out.push_back(cur);
    for (auto& s : out) {
        while (!s.empty() && isspace(s.front())) s.erase(s.begin());
        while (!s.empty() && isspace(s.back()))  s.pop_back();
    }
    return out;
}

bool WarehouseIO::file_empty_or_missing(const string& path) {
    ifstream f(path);
    if (!f.good()) return true;
    string line;
    while (getline(f, line))
        for (char c : line)
            if (!isspace(c)) return false;
    return true;
}

vector<Point> WarehouseIO::read_warehouse(const string& path) {
    vector<Point> res;
    ifstream f(path);
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() >= 2)
            res.push_back({stod(v[0]), stod(v[1])});
    }
    return res;
}

vector<Obstacle> WarehouseIO::read_obstacles(const string& path) {
    vector<Obstacle> res;
    if (file_empty_or_missing(path)) return res;
    ifstream f(path);
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() >= 4)
            res.push_back({stod(v[0]), stod(v[1]), stod(v[2]), stod(v[3])});
    }
    return res;
}

vector<pair<double,double>> WarehouseIO::read_ceiling(const string& path) {
    vector<pair<double,double>> res;
    ifstream f(path);
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() >= 2)
            res.push_back({stod(v[0]), stod(v[1])});
    }
    sort(res.begin(), res.end());
    return res;
}

vector<BayType> WarehouseIO::read_bays(const string& path) {
    vector<BayType> res;
    ifstream f(path);
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() >= 7)
            res.push_back({stoi(v[0]), stoi(v[1]), stoi(v[2]), stoi(v[3]),
                           stoi(v[4]), stoi(v[5]), stoi(v[6])});
    }
    return res;
}