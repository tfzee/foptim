#pragma once
#include <cstdint>
#include <cstdlib>

namespace foptim {
namespace fmir {
class VReg;
enum class CReg : uint8_t;
class MArgument;
class MInstr;
class MBB;
class MFunc;
enum class Type : uint16_t;
} // namespace fmir

extern thread_local char thread_name[11];
namespace utils {}

namespace optim {
struct LiveRange;
class KnownBits;
} // namespace optim

namespace fir {
class Function;
class FunctionR;
class ValueR;
class BasicBlock;
class BBArgument;
class Instr;
class InstrData;
struct BBRefWithArgs;
class TypeR;
class AnyType;
struct ConstantValue;
class ConstantValueR;
class Attribute;
class Use;
struct IRLocation;
struct ContextData;
class InstrData;
class Use;
class ValueR;
class BasicBlock;
class TypeR;
struct ConstantValue;
class BasicBlockData;
class BBArgument;
class Instr;
class Builder;
} // namespace fir

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using i128 = __int128_t;
using u128 = __uint128_t;

using f32 = float;
using f64 = double;

using size_t = std::size_t;

#define fori(input) for (size_t i = 0; i < (input).size(); i++)

#define COLOR_OPTIMF 0xEE0000
#define COLOR_OPTIMM 0xEE4444
#define COLOR_ANALY 0x0000EE

}; // namespace foptim
