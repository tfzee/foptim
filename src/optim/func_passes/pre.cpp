#include "pre.hpp"

namespace foptim::optim {
static void
init_transp_antloc(BBData &transp, BBData &antloc, BBData &comp,
                   const fir::Function &func, const TVec<fir::Instr> &exprs,
                   const TMap<fir::Instr, TVec<fir::Instr>> &expr_to_instrs) {

  for (size_t expr_i = 0; expr_i < exprs.size(); expr_i++) {
    // transparency
    for (const auto &arg : exprs[expr_i]->get_args()) {
      if (arg.is_instr()) {
        const fir::Instr instr = arg.as_instr();
        size_t bb_indx = func.bb_id(instr->get_parent());
        transp[bb_indx][expr_i].set(false);
      } else if (arg.is_bb_arg()) {
        auto bbarg = arg.as_bb_arg();
        size_t bb_indx = func.bb_id(bbarg->get_parent());
        transp[bb_indx][expr_i].set(false);
      }
    }
  }

  // need to do 2 loops sicne we reuse teh transp result
  for (size_t expr_i = 0; expr_i < exprs.size(); expr_i++) {
    // Since we in SSA form if the current block declares a arugment
    // it must come prior to the definition as such antloc jsut checks if its
    // transparent in its own block

    // since we only care about expressions tho we would need to find every
    // occurence of a expresion to mark it

    for (const auto &act_instr_with_expr : expr_to_instrs.at(exprs[expr_i])) {
      size_t act_bb_i = func.bb_id(act_instr_with_expr->get_parent());
      comp[act_bb_i][expr_i].set(true);
      if (transp[act_bb_i][expr_i]) {
        antloc[act_bb_i][expr_i].set(true);
      }
    }
  }
}

static inline void execute(const BBData &save, const BBData &insert_sin,
                           const DBBData & /*insert_doub*/,
                           const BBData &redund,
                           const std::span<fir::Instr> exprs,
                           const std::span<fir::BasicBlock> bbs) {
  const size_t n_bbs = bbs.size();
  TVec<fir::ValueR> repl_map(insert_sin[0].size());

  for (size_t bb_id = 0; bb_id < n_bbs; bb_id++) {
    const auto &cbb = bbs[bb_id];

    for (auto save_id : save[bb_id]) {
      // for ssa form saving is kinda useless so we just store the first into
      // our map
      auto res_instr =
          std::find_if(cbb->instructions.begin(), cbb->instructions.end(),
                       [exprs, save_id](const fir::Instr &v) {
                         return v->eql_expr(*exprs[save_id].operator->());
                       });
      if (res_instr == cbb->instructions.end()) {

        // print << bbs[0]->get_parent() << "\n";

        // print << "INBB: " << bb_id << "\n";
        // print << "SAVE EXPR: " << exprs[save_id] << "\n";
        // print << "EXPR NOT FOUND IN BB\n";

        assert(false);
      }
      repl_map.at(save_id) = fir::ValueR(*res_instr);
    }

    for (auto insert_id : insert_sin[bb_id]) {
      auto bb = fir::Builder(bbs[bb_id]);
      bb.at_penultimate(bbs[bb_id]);
      fir::Instr copy = bb.insert_copy(exprs[insert_id]);

      repl_map.at(insert_id) = fir::ValueR(copy);
    }

    {
      for (size_t bb2_id = 0; bb2_id < n_bbs; bb2_id++) {
      }
    }

    for (auto redund_id : redund[bb_id]) {
      // find redundant instrs
      // we search for the redundant instr
      for (auto instr : cbb->instructions) {
        if (instr->eql_expr(*exprs[redund_id].operator->())) {
          // replace all uses with the value we saved in the map
          instr->replace_all_uses(repl_map.at(redund_id));
        }
      }
    }
  }
}

void EPathPRE::apply(fir::Context & /*ctx*/, fir::Function &func) {
  ZoneScopedN("EPathPRE");
  CFG cfg{func};
  assert(cfg.entry == 0);

  // expr -> list of instruction that use that expresion;
  TMap<fir::Instr, TVec<fir::Instr>> expr_to_instrs;
  expr_to_instrs.reserve(func.n_instrs());

  // list of all unique exprs;
  TVec<fir::Instr> exprs;
  exprs.reserve(func.n_instrs());

  {
    ZoneScopedN("Setup");
    for (auto bb : func.basic_blocks) {
      for (size_t i = 0; i + 1 < bb->instructions.size(); i++) {
        auto target_instr = bb->instructions[i];
        // skip stuff with sideeffects
        if (target_instr->has_pot_sideeffects()) {
          continue;
        }

        auto expr = std::find_if(
            exprs.begin(), exprs.end(), [target_instr](auto instr) {
              auto res = instr->eql_expr(*target_instr.operator->());
              return res;
            });
        if (expr == exprs.end()) {
          exprs.push_back(target_instr);
          expr_to_instrs.insert({target_instr, TVec<fir::Instr>{target_instr}});
        } else {
          expr_to_instrs.at(*expr).push_back(target_instr);
        }
      }
    }
  }

  const size_t n_bbs = func.basic_blocks.size();
  const size_t n_exprs = exprs.size();
  const BitSet<> empty_bitset(n_exprs, false);
  const BitSet<> full_bitset(n_exprs, true);

  // precomputed
  BBData transp{};
  transp.resize(n_bbs, full_bitset);
  BBData comp{};
  comp.resize(n_bbs, empty_bitset);
  BBData antloc{};
  antloc.resize(n_bbs, empty_bitset);

  init_transp_antloc(transp, antloc, comp, func, exprs, expr_to_instrs);

  // for (size_t bb_id = 0; bb_id < n_bbs; bb_id++) {
  // fmt::println("{}", bb_id);
  // fmt::println("Comp  : {}", comp[bb_id]);
  // fmt::println("Antloc: {}", antloc[bb_id]);
  // fmt::println("Transp: {}", transp[bb_id]);
  // }

  // dynamic
  BBData av_in{};
  av_in.resize(n_bbs, full_bitset);
  BBData av_out{};
  av_out.resize(n_bbs, full_bitset);
  BBData ant_in{};
  ant_in.resize(n_bbs, full_bitset);
  BBData ant_out{};
  ant_out.resize(n_bbs, full_bitset);
  BBData eps_in{};
  eps_in.resize(n_bbs, full_bitset);
  BBData eps_out{};
  eps_out.resize(n_bbs, full_bitset);
  BBData redund{};
  redund.resize(n_bbs, full_bitset);
  BBData insert_sin{};
  insert_sin.resize(n_bbs, full_bitset);
  DBBData insert_doub{};
  insert_doub.resize(n_bbs);
  for (size_t i = 0; i < insert_doub.size(); i++) {
    insert_doub[i].resize(n_bbs, empty_bitset);
    // insert_doub[i].resize(n_bbs, i == cfg.entry ? empty_bitset :
    // full_bitset); insert_doub[i][0].assign(empty_bitset);
  }
  BBData sa_out{};
  sa_out.resize(n_bbs, full_bitset);
  BBData sa_in{};
  sa_in.resize(n_bbs, full_bitset);
  BBData save{};
  save.resize(n_bbs, full_bitset);

  // av_in[cfg.entry] = empty_bitset;
  // av_out[cfg.entry] = empty_bitset;
  // ant_in[cfg.entry] = empty_bitset;
  // ant_out[cfg.entry] = empty_bitset;
  // eps_in[cfg.entry] = empty_bitset;
  // eps_out[cfg.entry] = empty_bitset;
  // redund[cfg.entry] = empty_bitset;
  // insert_sin[cfg.entry] = empty_bitset;
  // sa_out[cfg.entry] = empty_bitset;
  // sa_in[cfg.entry] = empty_bitset;
  // save[cfg.entry] = empty_bitset;

  std::deque<u32, utils::TempAlloc<u32>> worklist = {};
  for (size_t i = 0; i < cfg.bbrs.size(); i++) {
    worklist.push_back(i);
  }

  BitSet av_out_new{n_exprs, false};
  BitSet ant_in_new{n_exprs, false};
  BitSet ant_out_new{n_exprs, false};
  BitSet eps_in_new{n_exprs, false};
  BitSet eps_out_new{n_exprs, false};
  BitSet insert_sin_new{n_exprs, false};

  BitSet sa_in_new{n_exprs, false};
  BitSet save_new{n_exprs, false};
  BitSet redund_new{n_exprs, false};
  BitSet doub_update_const{n_exprs, false};
  BitSet av_in_new{n_exprs, false};
  TVec<BitSet<>> insert_doub_new = {};
  insert_doub_new.resize(n_bbs, empty_bitset);
  BitSet sa_out_new{n_exprs, false};

  {
    ZoneScopedN("Dataflow");
    while (!worklist.empty()) {
      u32 curr_bb = worklist.front();
      worklist.pop_front();

      av_in_new.reset(false);
      av_out_new.assign(av_in[curr_bb]).mul(transp[curr_bb]).add(comp[curr_bb]);
      ant_in_new.assign(ant_out[curr_bb])
          .mul(transp[curr_bb])
          .add(antloc[curr_bb]);
      ant_out_new.reset(false);
      eps_in_new.reset(false);
      eps_out_new.assign(eps_in[curr_bb]).mul_not(antloc[curr_bb]);
      redund_new.assign(eps_in[curr_bb])
          .add(av_in[curr_bb])
          .mul(antloc[curr_bb]);
      insert_sin_new.reset(false);

      // for (auto &s : insert_doub_new) {
      //   s.reset(false);
      // }

      sa_out_new.reset(false);
      sa_in_new.assign(sa_out[curr_bb]).mul_not(comp[curr_bb]);
      save_new.assign(redund[curr_bb])
          .mul(transp[curr_bb])
          .negate()
          .mul(comp[curr_bb])
          .mul(sa_out[curr_bb]);

      // all other blocks
      // constant stuff for each
      doub_update_const.assign(av_out[curr_bb])
          .negate()
          .mul_not(eps_out[curr_bb])
          .mul_not(insert_sin[curr_bb]);
      for (size_t other = 0; other < n_bbs; other++) {
        insert_doub_new[other].assign(doub_update_const).mul(eps_in[other]);
        // insert_doub_new[other] = doub_update_const * eps_in[other];
      }

      // predecessor
      for (size_t i = 0; i < cfg.bbrs[curr_bb].pred.size(); i++) {
        auto pred = cfg.bbrs[curr_bb].pred[i];
        if (i == 0) {
          av_in_new.assign(av_out[pred]);
        } else {
          av_in_new.mul(av_out[pred]);
        }
        eps_in_new.add(av_out[pred]).add(eps_out[pred]);
      }

      // successors
      for (size_t i = 0; i < cfg.bbrs[curr_bb].succ.size(); i++) {
        auto succ = cfg.bbrs[curr_bb].succ[i];
        if (i == 0) {
          ant_out_new.assign(ant_in[succ]);
          insert_sin_new.assign(eps_in[succ]);
        } else {
          ant_out_new.mul(ant_in[succ]);
          insert_sin_new.mul(eps_in[succ]);
        }
        sa_out_new.add(eps_in[succ]).add(redund[succ]).add(sa_in[succ]);
      }

      eps_in_new.mul(ant_in[curr_bb]).mul_not(av_in[curr_bb]);
      sa_out_new.mul(av_out[curr_bb]);
      insert_sin_new.mul_not(av_out[curr_bb]).mul_not(eps_out[curr_bb]);

      {

        // FOR TESTING CONVERGENCE
        {
          bool something_changed =
              av_in[curr_bb] != av_in_new || av_out[curr_bb] != av_out_new ||
              ant_in[curr_bb] != ant_in_new ||
              ant_out[curr_bb] != ant_out_new ||
              eps_in[curr_bb] != eps_in_new ||
              eps_out[curr_bb] != eps_out_new ||
              redund[curr_bb] != redund_new ||
              insert_sin[curr_bb] != insert_sin_new ||
              sa_out[curr_bb] != sa_out_new || sa_in[curr_bb] != sa_in_new ||
              save[curr_bb] != save_new;
          for (size_t i = 0; i < cfg.bbrs.size(); i++) {
            if (insert_doub[curr_bb][i] != insert_doub_new[i]) {
              something_changed = true;
              break;
            }
          }
          if (something_changed) {
            for (size_t other = 0; other < n_bbs; other++) {
              worklist.push_back(other);
            }
          }
        }

        if (eps_in[curr_bb] != eps_in_new) {
          for (size_t other = 0; other < n_bbs; other++) {
            worklist.push_back(other);
          }
        }
        if (av_out[curr_bb] != av_out_new || eps_out[curr_bb] != eps_out_new) {
          for (auto succ : cfg.bbrs[curr_bb].succ) {
            worklist.push_back(succ);
          }
        }
        // if succ impacts pred
        if (ant_in[curr_bb] != ant_in_new /*|| eps_in[curr_bb] != eps_in_new*/
            || redund[curr_bb] != redund_new || sa_in[curr_bb] != sa_in_new) {
          for (auto pred : cfg.bbrs[curr_bb].pred) {
            worklist.push_back(pred);
          }
        }

        if (av_out[curr_bb] != av_out_new || ant_in[curr_bb] != ant_in_new ||
            av_in[curr_bb] != av_in_new ||
            insert_sin[curr_bb] != insert_sin_new ||
            sa_in[curr_bb] != sa_in_new || eps_out[curr_bb] != eps_out_new ||
            redund[curr_bb] != redund_new) {
          worklist.push_back(curr_bb);
        }

        av_in[curr_bb].assign(av_in_new);
        av_out[curr_bb].assign(av_out_new);
        ant_in[curr_bb].assign(ant_in_new);
        ant_out[curr_bb].assign(ant_out_new);
        eps_in[curr_bb].assign(eps_in_new);
        eps_out[curr_bb].assign(eps_out_new);
        redund[curr_bb].assign(redund_new);
        insert_sin[curr_bb].assign(insert_sin_new);
        for (size_t i = 0; i < cfg.bbrs.size(); i++) {
          insert_doub[curr_bb][i].assign(insert_doub_new[i]);
        }
        sa_out[curr_bb].assign(sa_out_new);
        sa_in[curr_bb].assign(sa_in_new);
        save[curr_bb].assign(save_new);
      }
    }
  }

  {
    ZoneScopedN("Execute");
    execute(save, insert_sin, insert_doub, redund, exprs, func.get_bbs());
  }
}
} // namespace foptim::optim
