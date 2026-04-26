#include "case_io.h"

bool is_test_case_dir(const fs::path& dir) {
    if (!fs::is_directory(dir)) {
        return false;
    }

    static const vector<string> required_files = {
        "warehouse.csv",
        "obstacles.csv",
        "ceiling.csv",
        "types_of_bays.csv"
    };

    for (const string& file : required_files) {
        if (!fs::is_regular_file(dir / file)) {
            return false;
        }
    }

    return true;
}

vector<string> discover_test_cases() {
    vector<string> found;

    for (const auto& entry : fs::directory_iterator(fs::current_path())) {
        if (is_test_case_dir(entry.path())) {
            found.push_back(entry.path().filename().string());
        }
    }

    sort(found.begin(), found.end());

    vector<string> ordered;

    for (const string& preferred : PREFERRED_CASE_ORDER) {
        auto it = find(found.begin(), found.end(), preferred);

        if (it != found.end()) {
            ordered.push_back(preferred);
            found.erase(it);
        }
    }

    ordered.insert(ordered.end(), found.begin(), found.end());
    return ordered;
}
