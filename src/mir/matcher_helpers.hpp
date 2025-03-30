#pragma once
#include "utils/vec.hpp"

namespace foptim::fir {
class ValueR;
}
namespace foptim::fmir {
enum class Type : u16;
class DumbRegAlloc;
class MArgument;
class MInstr;
struct MatchResult;
struct ExtraMatchData;

MArgument get_or_insert_bbarg_mapping(fir::BBArgument arg, MatchResult &res,
                                      ExtraMatchData &data);
MArgument valueToArgPtr(fir::ValueR val, Type type_id, DumbRegAlloc &alloc);
MArgument valueToArg(fir::ValueR val, IRVec<MInstr> &res, DumbRegAlloc &alloc);
MArgument valueToArgConst(fir::ValueR val, IRVec<MInstr> &res,
                          DumbRegAlloc &alloc);
void setup_callargs(fir::Instr &call_instr, MatchResult &res,
                    ExtraMatchData &data);
} // namespace foptim::fmir
