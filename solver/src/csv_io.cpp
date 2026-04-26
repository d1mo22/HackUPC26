#include "csv_io.h"

vector<string> split_csv_line(string line) {
    vector<string> out;
    string cur;

    for (char c : line) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }

    out.push_back(cur);

    for (auto& s : out) {
        while (!s.empty() && isspace(s.front())) s.erase(s.begin());
        while (!s.empty() && isspace(s.back())) s.pop_back();
    }

    return out;
}

bool file_empty_or_missing(const string& path) {
    ifstream f(path);

    if (!f.good()) return true;

    string line;

    while (getline(f, line)) {
        for (char c : line) {
            if (!isspace(c)) return false;
        }
    }

    return true;
}

bool try_stod(const string& s, double& out) {
    if (s.empty()) return false;
    try { size_t pos = 0; out = stod(s, &pos); return pos > 0; }
    catch (...) { return false; }
}

bool try_stoi(const string& s, int& out) {
    if (s.empty()) return false;
    try { size_t pos = 0; out = stoi(s, &pos); return pos > 0; }
    catch (...) { return false; }
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
    if (file_empty_or_missing(path)) return res;

    ifstream f(path);
    string line;

    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = split_csv_line(line);
        if (v.size() < 4) continue;
        double a, b, c, d;
        if (!try_stod(v[0], a) || !try_stod(v[1], b) ||
            !try_stod(v[2], c) || !try_stod(v[3], d)) continue;
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
