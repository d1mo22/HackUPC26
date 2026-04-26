#pragma once

#include "csv_io.h"
#include <string>
#include <vector>

bool is_test_case_dir(const fs::path& dir);
std::vector<std::string> discover_test_cases();
