#pragma once
#include <vector>


struct Point  { 
    double x, y; 
};


struct BayType {
    int id, w, d, h, gap, loads, price;
};

struct PlacedBay {
    int    id;
    double x, y;
    int    w, d, h, gap;
    int    angle;
    int    price, loads;
};

struct Obstacle {
    double x, y, w, d;
};