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
MArgument valueToArg(fir::ValueR val, TVec<MInstr> &res, DumbRegAlloc &alloc);
TVec<MArgument> valueToArgStruct(fir::ValueR val, TVec<MInstr> &res,
                                 DumbRegAlloc &alloc);
MArgument valueToArgConst(fir::ValueR val, IRVec<MInstr> &res,
                          DumbRegAlloc &alloc);
void setup_callargs(fir::Instr &call_instr, MatchResult &res,
                    ExtraMatchData &data);
void setup_va_start(fir::Instr &call_instr, MatchResult &res,
                    ExtraMatchData &data);
void setup_va_end(fir::Instr &call_instr, MatchResult &res,
                  ExtraMatchData &data);

bool generate_lea_from_cmult(MArgument res_reg, VReg helper_reg, VReg arg0,
                             i128 consti_val, TVec<MInstr> &result,
                             Type res_ty);
}  // namespace foptim::fmir
