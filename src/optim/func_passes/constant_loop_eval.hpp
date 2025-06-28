#pragma once
#include <algorithm>

#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/interpreter/interpreter.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"
#include "utils/bitset.hpp"

namespace foptim::optim {
class ConstLoopEval final : public FunctionPass {
  bool applicable(fir::Function &f, LoopInfo &info) {
    // can only have 1 entry
    if (f.basic_blocks[info.head]->get_n_uses() != 1 + info.tails.size()) {
      return false;
    }
    for (auto id : info.body_nodes) {
      for (auto use : f.basic_blocks[id]->uses) {
        switch (use.type) {
        case fir::UseType::NormalArg:
        case fir::UseType::BBArg:
          return false;
        case fir::UseType::BB:
        }
        for (size_t bb_arg_id = 0; bb_arg_id < f.basic_blocks[id]->args.size();
             bb_arg_id++) {
          auto &v = use.user->bbs[use.argId].args[bb_arg_id];
          if (v.is_constant() &&
              (v.as_constant()->is_func() || v.as_constant()->is_global())) {
            return false;
          }
          if (v.is_instr() || v.is_bb_arg()) {
            u32 bb_id = 0;
            if (v.is_instr()) {
              auto arg_instr = v.as_instr();
              bb_id = f.bb_id(arg_instr->get_parent());
            } else {
              auto bbarg = v.as_bb_arg();
              bb_id = f.bb_id(bbarg->get_parent());
            }
            if (std::ranges::find(info.body_nodes, bb_id) ==
                info.body_nodes.end()) {
              return false;
            }
          }
        }
      }
      for (auto instr : f.basic_blocks[id]->instructions) {
        switch (instr->instr_type) {
        case fir::InstrType::FCmp:
        case fir::InstrType::UnaryInstr:
        case fir::InstrType::Conversion:
        case fir::InstrType::SelectInstr:
        case fir::InstrType::Intrinsic:
          // TODO: later one these above too
        case fir::InstrType::AllocaInstr:
        case fir::InstrType::ExtractValue:
        case fir::InstrType::InsertValue:
        case fir::InstrType::ITrunc:
        case fir::InstrType::CallInstr:
        case fir::InstrType::ReturnInstr:
        case fir::InstrType::SwitchInstr:
        case fir::InstrType::Unreachable:
        case fir::InstrType::LoadInstr:
        case fir::InstrType::StoreInstr:
        case fir::InstrType::VectorInstr:
          return false;
        case fir::InstrType::BinaryInstr:
        case fir::InstrType::ICmp:
        case fir::InstrType::CondBranchInstr:
        case fir::InstrType::BranchInstr:
        case fir::InstrType::ZExt:
        case fir::InstrType::SExt:
        }
        for (auto &bb : instr->bbs) {
          for (auto &arg : bb.args) {
            if (arg.is_constant() && (arg.as_constant()->is_func() ||
                                      arg.as_constant()->is_global())) {
              return false;
            }
            if (arg.is_instr() || arg.is_bb_arg()) {
              u32 bb_id = 0;
              if (arg.is_instr()) {
                auto arg_instr = arg.as_instr();
                bb_id = f.bb_id(arg_instr->get_parent());
              } else {
                auto bbarg = arg.as_bb_arg();
                bb_id = f.bb_id(bbarg->get_parent());
              }
              if (std::ranges::find(info.body_nodes, bb_id) ==
                  info.body_nodes.end()) {
                return false;
              }
            }
          }
        }
        for (auto arg : instr->args) {
          if (arg.is_constant() && (arg.as_constant()->is_func() ||
                                    arg.as_constant()->is_global())) {
            return false;
          }
          if (arg.is_instr() || arg.is_bb_arg()) {
            u32 bb_id = 0;
            if (arg.is_instr()) {
              auto arg_instr = arg.as_instr();
              bb_id = f.bb_id(arg_instr->get_parent());
            } else {
              auto bbarg = arg.as_bb_arg();
              bb_id = f.bb_id(bbarg->get_parent());
            }
            if (std::ranges::find(info.body_nodes, bb_id) ==
                info.body_nodes.end()) {
              return false;
            }
          }
        }
      }
    }
    return true;
  }

public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("ConstLoopEval");
    // if a loop only contains 'pure' instrutions that can be evaluated at
    // compile time and all the input dependencies are constant + we have a
    // mustprogress attribute(no infinite loops) we should constant evaluate
    // the loop
    if (!func.must_progress) {
      return;
    }
    CFG cfg{func};
    Dominators dom{cfg};
    LoopInfoAnalysis info{dom};

    for (auto loop_iter = info.info.begin(); loop_iter != info.info.end();
         loop_iter++) {
      auto &loop = *loop_iter;

      if (applicable(func, loop)) {
        // find the entry bb that calls the loop header initially
        //  we know there is exactly one (because we check for it in teh
        //  applicable function)
        //

        u32 enter_bb_id = 0;
        u32 enter_bb_instr_id = 0;
        for (auto use : func.basic_blocks[loop.head]->uses) {
          auto use_bb = use.user->get_parent();
          auto use_bb_id = func.bb_id(use_bb);
          if (std::ranges::find(loop.tails, use_bb_id) == loop.tails.end()) {
            enter_bb_id = use_bb_id;
            enter_bb_instr_id = use_bb->n_instrs() - 1;
            break;
          }
        }
        // TODO: this only works if the entry into the loop is a simple branch
        // instruction if its not we *need* to set the cbranch/switch arg to be
        // set correctly so it enters the loop
        if (!func.basic_blocks[enter_bb_id]
                 ->instructions[enter_bb_instr_id]
                 ->is(fir::InstrType::BranchInstr)) {
          return;
        }

        bool failed = false;
        fir::intepreter::Interpreter inter{&func, enter_bb_id,
                                           enter_bb_instr_id};

        {
          ZoneScopedN("Interpret");
          size_t iter = 0;
          while (true) {
            if (!inter.step_till_end_of_bb()) {
              TODO("failed?");
              failed = true;
              break;
            }
            iter++;
            auto ip = inter.get_ip();
            if ((void *)ip.func == (void *)&func &&
                std::ranges::find(loop.body_nodes, ip.bb_id) ==
                    loop.body_nodes.end()) {
              break;
            }
            if (iter > 5000) {
              failed = true;
              break;
            }
          }
        }
        if (failed) {
          continue;
        }

        bool has_modified_anything = false;
        for (const auto &[v, c] : inter.get_values()) {
          auto vv = const_cast<fir::ValueR &>(v);
          // we can modify bb args outside of our loop
          if (vv.is_bb_arg()) {
            auto parent_id = cfg.get_bb_id(vv.as_bb_arg()->get_parent());
            if (std::ranges::find(loop.body_nodes, parent_id) ==
                loop.body_nodes.end()) {
              continue;
            }
          }
          if (vv.get_n_uses() > 0) {
            has_modified_anything = true;
          }
          vv.replace_all_uses(fir::ValueR{ctx->get_constant_value(c)});
        }
        if (!has_modified_anything) {
          continue;
        }
        // TODO cleanup all the cbranches
        // inter.dump_state();
        // fmt::println("EXIT BB {}", inter.get_ip().bb_id);
        // fmt::println("{}", func);
        // TODO("okak");
        cfg.update(func, false);
        dom.update(cfg);
        info.update(dom);
        loop_iter = info.info.begin();
      }
    }
  }
};
} // namespace foptim::optim
