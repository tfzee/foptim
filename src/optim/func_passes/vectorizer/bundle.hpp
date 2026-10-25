#pragma once
#include "optim/func_passes/vectorizer/plan.hpp"

namespace foptim::optim::vec {

// Straight line code: the lanes are the instructions of a seed bundle. The
// planner grows a tree from the seed through the operands, reduction seeds
// start at a horizontal reduction instead of at stores. Planning is read only
// (it may intern constants), the benefit is the scalar minus the vector cost.
// False if some operand has no recipe.
bool plan_bundle(fir::Context &ctx, const Features &features, Seed &seed,
                 bool reduction, Plan &plan);

// Emits the vector code of the plan and erases the scalars of the seed.
void apply_bundle(fir::Context &ctx, const Plan &plan, Seed &seed,
                  bool reduction);

}  // namespace foptim::optim::vec
