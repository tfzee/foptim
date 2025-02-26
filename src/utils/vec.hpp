#pragma once
#include "helpers.hpp"
#include "utils/arena.hpp"
#include "utils/logging.hpp"
#include <vector>

namespace foptim {

template <class Val, class Alloc = utils::FAlloc<Val>>
using FVec = std::vector<Val, Alloc>;

template <class Val, class Alloc = utils::TempAlloc<Val>>
using TVec = std::vector<Val, Alloc>;

template <class Val, class Alloc = utils::IRAlloc<Val>>
using IRVec = std::vector<Val, Alloc>;

} // namespace foptim


template <class T>
class fmt::formatter<foptim::TVec<T>>
    : public BaseIRFormatter<foptim::TVec<T>> {
public:
  appender format(foptim::TVec<T> const &k, format_context &ctx) const{
    auto app = ctx.out();
    for(const auto& elem: k){
      app = fmt::format_to(app, "{}", elem);
    }
    return app;
  }
};
