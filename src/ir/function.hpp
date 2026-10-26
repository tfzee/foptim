#pragma once
#include <memory>
#include <utility>

#include "basic_block.hpp"
#include "basic_block_ref.hpp"
#include "ir/access_meta.hpp"
#include "ir/attributable.hpp"
#include "ir/helpers.hpp"
#include "ir/libfunc.hpp"
#include "types_ref.hpp"
#include "utils/bitset.hpp"
#include "utils/string.hpp"
#include "utils/todo.hpp"

namespace foptim::optim {
struct FunctionAnalysisCache;
}

namespace foptim::fir {

// defined next to the AnalysisManager so Function does not need the analyses
struct AnalysisCacheDeleter {
  void operator()(optim::FunctionAnalysisCache *p) const;
};

class Function : public Attributable, public LockedUsed {
 public:
  enum class CallingConv { C, Dynamic };
  struct Attribs {
    CallingConv cc = CallingConv::C;
    Linkage linkage = Linkage::Internal;
    LinkVisibility linkvis = LinkVisibility::Default;
    u16 min_align = 16;
    u8 variadic : 1 = 0;
    // needs to make progress
    u8 must_progress : 1 = 0;
    // needs to return in finite steps
    u8 must_return : 1 = 0;
    u8 no_recurse : 1 = 0;
    u8 no_inline : 1 = 0;
    u8 no_return : 1 = 0;
    u8 cold : 1 = 0;
    u8 must_inline : 1 = 0;
    // created by region extraction, never split again
    u8 outlined : 1 = 0;
    u8 mem_write_only : 1 = 0;
    u8 mem_read_only : 1 = 0;
    u8 mem_read_none : 1 = 0;
    // set by the loader for declarations that are a known C library function
    LibFunc libfunc = LibFunc::None;

    // mark as potentially be able to be whole function vectorized (but verify
    // before applying since it can easliy be invalidated)
    u8 maybe_can_wfvec : 1 = 0;
  };

  ContextData *ctx;
  IRString name;
  FunctionTypeR func_ty;
  IRVec<BasicBlock> basic_blocks;
  // metadata
  Attribs attribs;
  // numbers the vector constant globals LegalizeVecs creates for this function
  u32 n_vec_constants = 0;
  // analyses cached by optim::AnalysisManager, see there
  std::unique_ptr<optim::FunctionAnalysisCache, AnalysisCacheDeleter>
      analysis_cache;
  // null while no access has metadata
  std::unique_ptr<AccessMetaTable> access_meta;
  // bit per FIR function pass that is known to be a no-op on the function in
  // its current state, see optim/pass_skipping.hpp. null = nothing known
  std::unique_ptr<utils::BitSet<u64, utils::IRAlloc<u64>>> clean_passes;

  Function(ContextData *ctx, IRString name, FunctionTypeR type)
      : ctx(ctx), name(std::move(name)), func_ty(type), basic_blocks({}) {}

  [[nodiscard]] const AccessMeta *get_access_meta(Instr i) const {
    return access_meta ? access_meta->get(i) : nullptr;
  }
  void set_access_meta(Instr i, AccessMeta m) {
    if (!access_meta) {
      if (m.empty()) {
        return;
      }
      access_meta = std::make_unique<AccessMetaTable>();
    }
    access_meta->set(i, std::move(m));
  }
  void erase_access_meta(Instr i) {
    if (access_meta) {
      access_meta->erase(i);
    }
  }
  // to is an instruction of this function, it loses its metadata if from has
  // none
  void copy_access_meta(const Function &from_func, Instr from, Instr to,
                        MetaCopy how = MetaCopy::All) {
    const AccessMeta *m = from_func.get_access_meta(from);
    if (m == nullptr || how == MetaCopy::None) {
      erase_access_meta(to);
      return;
    }
    set_access_meta(to, filter_meta(*m, how));
  }

  void dump_data_dependency_graph(const char *filename = "out.dot") const;
  bool is_decl() const { return basic_blocks.empty(); }
  IRString &getName() { return name; }
  const IRString &getName() const { return name; }

  [[nodiscard]] BasicBlock get_entry() const {
    ASSERT(!basic_blocks.empty());
    return basic_blocks[0];
  }
  auto &get_bbs() { return basic_blocks; }
  const auto &get_bbs() const { return basic_blocks; }

  size_t n_instrs() const {
    size_t n_instrs = 0;
    for (const auto bb : basic_blocks) {
      n_instrs += bb->n_instrs();
    }
    return n_instrs;
  }
  size_t n_bbs() const { return basic_blocks.size(); }

  /*
    @returns index of the given bb or aborts on invalid bb
  */
  [[nodiscard]] size_t bb_id(BasicBlock b) const;
  void append_bbr(BasicBlock bb) {
    bb->func = FunctionR{this};
    basic_blocks.push_back(bb);
  }
  bool verify() const;
};

}  // namespace foptim::fir
