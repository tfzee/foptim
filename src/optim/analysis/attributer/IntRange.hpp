#pragma once
#include "ir/instruction_data.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include <limits>

namespace foptim::optim {

// class IntRange final : public AttributeAnalysis {
// public:
//   PairLattice<i128, {0, 0},
//               {std::numeric_limits<i128>::min(),
//                std::numeric_limits<i128>::max()}>
//       lattice;
//   IntRange() = default;
//   ~IntRange() override = default;
//   void materialize_impl() override {}

//   Result update_impl(AttributerManager &m, Worklist &worklist) override {
//     if (associatedValue.is_constant()) {
//       TODO("impl");
//     }
//     if (associatedValue.is_bb_arg()) {
//       TODO("impl");
//     }
//     if (!associatedValue.is_instr()) {
//       return Result::Fixed;
//     }

//     auto instr = associatedValue.as_instr();
//     if (instr->is(fir::InstrType::BinaryInstr) &&
//         (fir::BinaryInstrSubType)instr->subtype ==
//             fir::BinaryInstrSubType::IntAdd) {
//       auto a1 = m.get_or_create_analysis<IntRange>(instr->args[0], &worklist);
//       auto a2 = m.get_or_create_analysis<IntRange>(instr->args[1], &worklist);
//       if (a1->lattice.isWorst() || a2->lattice.isWorst()) {
//         lattice.value = lattice.getWorst();
//         return Result::Changed;
//       } else {
//         TODO("impl");
//       }
//     }

//     return Result::Fixed;
//   }
// };

} // namespace foptim::optim
