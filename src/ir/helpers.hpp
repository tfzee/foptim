#pragma once
#include "utils/types.hpp"

namespace foptim::fir {

class BasicBlock;
class Context;
class ConstantValueR;
struct Global;

enum class Linkage {
  Internal,
  External,
  // definition might be overwritten *can't* inline
  Weak,
  // odr says every definition *must* be the same which allows for
  // optimization/discarding of unused definition
  WeakODR,
  // Similar to weak but different merging when linking this allows to be
  // discarded but not inlined
  LinkOnce,
  // ORD again says always the same definition which means it can be optimized
  // and discarded
  LinkOnceODR,
};
enum class LinkVisibility {
  Default,
  Hidden,
  Protected,
};

/*
Inserts a new basicblock redirects 'from's edges to 'to' into the new basicblock
And copies over the bb args from 'from' going to 'to' into the new basicblock
terminator
@returns the new basicblock
*/
BasicBlock insert_bb_between(BasicBlock from, BasicBlock to);
// BasicBlock split_block(BasicBlock a);

void convert_constant_init(u8 *output, ConstantValueR val, Global glob);

void generate_memset(fir::Context &fctx);
void generate_memcpy(fir::Context &fctx);
void generate_memmove(fir::Context &fctx);
void generate_fexp(fir::Context &fctx);
void generate_trunc(fir::Context &fctx);
void generate_trap(fir::Context &fctx);

}  // namespace foptim::fir
