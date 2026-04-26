#pragma once
#include <string>
#include <vector>
#include <utility>  
#include "types.h"

class WarehouseIO {
public:
    static std::vector<Point>                         read_warehouse(const std::string& path);
    static std::vector<Obstacle>                      read_obstacles(const std::string& path);
    static std::vector<std::pair<double,double>>      read_ceiling  (const std::string& path);
    static std::vector<BayType>                       read_bays     (const std::string& path);

private:
    static std::vector<std::string> split_csv_line(const std::string& line);
    static bool                     file_empty_or_missing(const std::string& path);
};