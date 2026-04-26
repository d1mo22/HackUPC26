#pragma once
#include "scoring.h"

// Forward declarations of all operator functions
bool add_bay(
    vector<PlacedBay>& sol,
    const OperatorContext& ctx,
    const vector<int>& angles_source = ANGLES
);

void fill_aggressive(vector<PlacedBay>& sol, const OperatorContext& ctx);
void remove_k_and_refill(vector<PlacedBay>& sol, const OperatorContext& ctx);
void upgrade_bay(vector<PlacedBay>& sol, const OperatorContext& ctx);
void replace_bay(vector<PlacedBay>& sol, const OperatorContext& ctx);
void split_bay(vector<PlacedBay>& sol, const OperatorContext& ctx);
void shared_gap_refill(vector<PlacedBay>& sol, const OperatorContext& ctx);
void rotate_compact_and_add(vector<PlacedBay>& sol, const OperatorContext& ctx);
void add_45_degree_bay(vector<PlacedBay>& sol, const OperatorContext& ctx);
void position_perturb_and_add(vector<PlacedBay>& sol, const OperatorContext& ctx);

// Operator table entry
struct OperatorEntry {
    const char* name;
    void (*fn)(vector<PlacedBay>&, const OperatorContext&);
    int weight_axis;    // times to push into active_ops for axis-aligned warehouses
    int weight_angled;  // times to push for angled/mixed warehouses
};

extern const OperatorEntry OPERATORS[NUM_OPERATORS];

vector<int> build_active_ops(bool axis_aligned);
