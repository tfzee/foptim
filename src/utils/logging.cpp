// #include "logging.hpp"
// #include "ir/attribute.hpp"
// #include "ir/basic_block_ref.hpp"
// #include "ir/constant_value.hpp"
// #include "ir/function.hpp"
// #include "ir/instruction.hpp"
// #include "ir/instruction_data.hpp"
// #include "ir/types.hpp"
// #include "ir/use.hpp"
// #include "ir/value.hpp"
// #include "mir/func.hpp"
// #include "mir/instr.hpp"
// #include "optim/analysis/live_variables.hpp"

// namespace foptim::utils {

// #define getColor(r, g, b) "\033[38;2;" #r ";" #g ";" #b "m"

// #define RESET "\033[39m\033[49m"
// #define BLUE getColor(66, 135, 245)
// #define ORANGE getColor(250, 185, 95)
// #define RED getColor(255, 100, 100)
// #define GREEN getColor(103, 230, 107)
// #define BLUEGREEN getColor(51, 255, 135)

// Printer Printer::operator<<(const i128 val) const {
//   auto &dest = std::cout;
//   std::ostream::sentry s(dest);
//   if (s) {
//     __uint128_t tmp = val < 0 ? -val : val;
//     char buffer[128];
//     char *d = std::end(buffer);
//     do {
//       --d;
//       *d = "0123456789"[tmp % 10];
//       tmp /= 10;
//     } while (tmp != 0);
//     if (val < 0) {
//       --d;
//       *d = '-';
//     }
//     int len = std::end(buffer) - d;
//     if (dest.rdbuf()->sputn(d, len) != len) {
//       dest.setstate(std::ios_base::badbit);
//     }
//   }
//   return *this;
// }
// Printer Printer::operator<<(const i64 val) const {
//   std::cout << val;
//   return *this;
// }
// Printer Printer::operator<<(const i32 val) const {
//   std::cout << val;
//   return *this;
// }
// Printer Printer::operator<<(const i16 val) const {
//   std::cout << val;
//   return *this;
// }
// Printer Printer::operator<<(const i8 val) const {
//   std::cout << val;
//   return *this;
// }
// Printer Printer::operator<<(const u64 val) const {
//   std::cout << val;
//   return *this;
// }
// Printer Printer::operator<<(const u32 val) const {
//   std::cout << val;
//   return *this;
// }
// Printer Printer::operator<<(const u16 val) const {
//   std::cout << val;
//   return *this;
// }
// Printer Printer::operator<<(const u8 val) const {
//   std::cout << val;
//   return *this;
// }
// Printer Printer::operator<<(const bool val) const {
//   std::cout << val;
//   return *this;
// }
// Printer Printer::operator<<(const f32 val) const {
//   std::cout << val;
//   return *this;
// }
// Printer Printer::operator<<(const f64 val) const {
//   std::cout << val;
//   return *this;
// }

// Printer Printer::operator<<(PaddingT pad) const {
//   (void)pad;
//   for (u32 i = indent; i > 0; i--) {
//     std::cout << " ";
//   }
//   return *this;
// }

// // Printer Printer::operator<<(const foptim::utils::BitSet &bitset) const {
// //   for (auto bit : bitset._data) {
// //     std::cout << bit;
// //   }
// //   return *this;
// // }

// Printer Printer::operator<<(const foptim::fir::Use &use) const {
//   *this << (void *)use.user.get_raw_ptr();
//   switch (use.type) {
//   case fir::UseType::NormalArg:
//     *this << "[N " << use.argId << "]";
//     break;
//   case fir::UseType::BB:
//     *this << "[B " << use.argId << "]";
//     break;
//   case fir::UseType::BBArg:
//     *this << "[BA " << use.argId << "; " << use.bbArgId << "]";
//     break;
//   }
//   return *this;
// }
// Printer Printer::operator<<(const foptim::optim::LiveRange &live) const {
//   return *this << live.bb << "(" << live.start << ", " << live.end << ")";
// }

// Printer Printer::operator<<(const foptim::fmir::MArgument &value) const {
//   *this << BLUE;
//   switch (value.type) {
//   case fmir::MArgument::ArgumentType::MemLabel:
//     *this << "[" << value.label.c_str() << "]: " << value.ty;
//     break;
//   case fmir::MArgument::ArgumentType::MemImmLabel:
//     *this << "[" << value.label.c_str() << " + " << value.imm << "]"
//           << value.ty;
//     break;
//   case fmir::MArgument::ArgumentType::Label:
//     *this << value.label.c_str();
//     break;
//   case fmir::MArgument::ArgumentType::Imm: {
//     if (value.ty == fmir::Type::Float32) {
//       *this << value.immf << "f";
//     } else if (value.ty == fmir::Type::Float64) {
//       *this << value.immf << "d";
//     } else {
//       *this << value.imm << ":" << value.ty;
//     }
//     break;
//   }
//   case fmir::MArgument::ArgumentType::VReg:
//     *this << value.reg << ":" << value.ty;
//     break;
//   case fmir::MArgument::ArgumentType::MemVReg:
//     *this << "[" << value.reg << "]:" << value.ty;
//     break;
//   case fmir::MArgument::ArgumentType::MemVRegVReg:
//     *this << "[" << value.reg << " + " << value.indx << "]:" << value.ty;
//     break;
//   case fmir::MArgument::ArgumentType::MemImm:
//     *this << "[" << value.imm << "]:" << value.ty;
//     break;
//   case fmir::MArgument::ArgumentType::MemImmVReg:
//     *this << "[" << value.reg << " + " << value.imm << "]:" << value.ty;
//     break;
//   case fmir::MArgument::ArgumentType::MemImmVRegVReg:
//     *this << "[" << value.reg << " + " << value.indx << " + " << value.imm
//           << "]:" << value.ty;
//     break;
//   case fmir::MArgument::ArgumentType::MemImmVRegScale:
//     *this << "[" << value.indx << "*" << value.scale << " + " << value.imm
//           << "]:" << value.ty;
//     break;
//   case fmir::MArgument::ArgumentType::MemVRegVRegScale:
//     *this << "[" << value.reg << " + " << value.indx << "*"
//           << (1 << value.scale) << "]:" << value.ty;
//     break;
//   case fmir::MArgument::ArgumentType::MemImmVRegVRegScale:
//     *this << "[" << value.reg << " + " << value.indx << "*"
//           << (1 << value.scale) << " + " << value.imm << "]:" << value.ty;
//     break;
//   }
//   return *this << RESET;
// }
// Printer Printer::operator<<(const foptim::fmir::MBB &bb) const {
//   for (const auto &instr : bb.instrs) {
//     this->pad(4) << padding() << instr << "\n";
//   }
//   return *this;
// }


// Printer Printer::operator<<(const foptim::fir::ConstantValueR v) const {
//   return *this << *v.get_raw_ptr();
// }

// Printer Printer::operator<<(foptim::fir::ValueR v) const {
//   std::visit(
//       [this](auto &&v) {
//         if constexpr (typeid(v) == typeid(fir::ConstantValueR) ||
//                       typeid(v) == typeid(fir::BBArgument)) {
//           *this << v;
//         } else if constexpr (typeid(v) == typeid(fir::BasicBlock)) {
//           *this << BLUE << (void *)(v.get_raw_ptr()) << RESET;
//         } else if constexpr (typeid(v) == typeid(fir::InvalidValue)) {
//           *this << "INVALID";
//         } else {
//           *this << (void *)v.get_raw_ptr();
//         }
//       },
//       v.get_raw());
//   return *this;
// }

// Printer Printer::operator<<(foptim::fir::BBArgument v) const {
//   return *this << GREEN << "BB[" << v.get_raw_ptr() << "]" << RESET;
// }

// Printer Printer::operator<<(const foptim::fir::Attribute &attrib) const {
//   std::visit(
//       [this](auto &&v) {
//         if constexpr (typeid(v) == typeid(fir::VoidAttrib)) {
//           return;
//         } else if constexpr (typeid(v) == typeid(IRString)) {
//           *this << ": " << v.c_str();
//         } else {
//           *this << ": " << v;
//         }
//       },
//       attrib.get_raw());
//   return *this;
// }
// Printer
// Printer::operator<<(const foptim::fir::BBRefWithArgs &bb_with_args) const {
//   *this << BLUE << (void *)bb_with_args.bb.get_raw_ptr() << RESET << "(";
//   if (!bb_with_args.args.empty()) {
//     *this << bb_with_args.args[0];
//     for (size_t i = 1; i < bb_with_args.args.size(); i++) {
//       *this << ", " << bb_with_args.args[i];
//     }
//   }
//   *this << ")";
//   return *this;
// }

// Printer Printer::operator<<(const foptim::fir::Instr instr) const {
//   return *this << instr.get_raw_ptr();
// }

// Printer Printer::operator<<(const foptim::fir::InstrData *instr) const {
//   *this << padding() << (void *)instr << " : " << instr->get_type() << " = "
//         << instr->get_name();

//   const auto &bb_args = instr->get_bb_args();
//   if (bb_args.size() > 0) {
//     *this << "<";
//     *this << bb_args[0];
//     for (size_t i = 1; i < bb_args.size(); i++) {
//       *this << ", " << bb_args[i];
//     }
//     *this << ">";
//   }

//   *this << "(";
//   const auto &args = instr->get_args();
//   if (args.size() > 0) {
//     *this << args[0];
//     for (size_t i = 1; i < args.size(); i++) {
//       *this << ", " << args[i];
//     }
//   }
//   *this << "){";

//   const auto &attribs = instr->get_attribs();
//   for (auto [key, value] : attribs) {
//     *this << key.c_str() << value << ", ";
//   }
//   *this << "}";
//   //*this << "          //Uses: " << instr->get_n_uses();
//   *this << "\n";
//   return *this;
// }

// Printer Printer::operator<<(foptim::fir::BasicBlock bb) const {
//   *this << padding() << BLUE << bb.get_raw_ptr() << RESET << "(";

//   auto &args = bb->args;
//   if (args.size() > 0) {
//     *this << args[0] << ": " << args[0]->get_type();
//     if (!args[0]->get_attribs().empty()) {
//       *this << "{";
//       const auto &attribs = args[0]->get_attribs();
//       for (auto [key, value] : attribs) {
//         *this << key.c_str() << value << ", ";
//       }
//       *this << "}";
//     }
//     for (size_t i = 1; i < args.size(); i++) {
//       *this << ", " << args[i] << ": " << args[i]->get_type();
//       if (!args[i]->get_attribs().empty()) {
//         *this << "{";
//         const auto &attribs = args[i]->get_attribs();
//         for (auto [key, value] : attribs) {
//           *this << key.c_str() << value << ", ";
//         }
//         *this << "}";
//       }
//       //*this << " uses:" << args[i].get_n_uses();
//     }
//   }

//   *this << "):";
//   //*this << "       //Uses:" << bb->get_n_uses()
//   *this << "\n";
//   for (fir::Instr instr : bb->get_instrs()) {
//     pad(2) << instr;
//   }
//   return *this;
// }

// Printer Printer::operator<<(foptim::fir::FunctionR func) const {
//   return *this << *func.operator->();
// }

// Printer Printer::operator<<(const foptim::fir::Function &func) const {
//   padding();
//   if (g_log_level <= level) {
//     std::cout << "\nfunc " << func.getName();

//     std::cout << "<" << "CC: ";
//     switch (func.cc) {
//     case fir::Function::CallingConv::C:
//       std::cout << "C";
//       break;
//     case fir::Function::CallingConv::Dynamic:
//       std::cout << "dyn";
//       break;
//     }
//     std::cout << ", LINK: ";
//     switch (func.linkage) {
//     case fir::Function::Linkage::Internal:
//       std::cout << "internal";
//       break;
//     case fir::Function::Linkage::External:
//       std::cout << "external";
//       break;
//     }
//     std::cout << ", ";
//     const auto &attribs = func.get_attribs();
//     for (auto [key, value] : attribs) {
//       *this << key.c_str() << value << ", ";
//     }

//     if (!func.get_bbs().empty()) {
//       std::cout << ">\n{\n";
//       for (fir::BasicBlock bb : func.get_bbs()) {
//         pad(2) << bb;
//       }
//       std::cout << "}";
//     } else {
//       std::cout << ">{}";
//     }
//   }
//   return *this;
// }

// Printer Printer::operator<<(const foptim::fir::IRLocation &loc) const {
//   switch (loc.type) {
//   case fir::IRLocation::LocationType::Function:
//     *this << (void *)loc.func.func;
//     break;
//   case fir::IRLocation::LocationType::Instruction:
//     *this << (void *)loc.func.func << " @BB " << loc.bb << " @I " << loc.instr;
//     break;
//   case fir::IRLocation::LocationType::BasicBlock:
//     *this << (void *)loc.func.func << " @BB " << loc.bb;
//     break;
//   case fir::IRLocation::LocationType::INVALID:
//     *this << " @INVALID ";
//     break;
//   }
//   return *this;
// }
// } // namespace foptim::utils
