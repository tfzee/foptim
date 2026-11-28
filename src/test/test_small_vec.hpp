#include <gtest/gtest.h>

#include <string>

#include "utils/SmallVec.hpp"

// erase of an element that is not trivially copyable has to move the rest
// down by one, with the elements inline and on the heap
TEST(SmallVecTest, EraseNonTrivial) {
  for (const size_t n : {3UL, 12UL}) {
    foptim::utils::SmallVec<std::string, 4> v;
    for (size_t i = 0; i < n; i++) {
      v.push_back(std::string(40, static_cast<char>('a' + i)));
    }
    v.erase(v.begin());
    ASSERT_EQ(v.size(), n - 1);
    for (size_t i = 0; i < v.size(); i++) {
      EXPECT_EQ(v[i], std::string(40, static_cast<char>('a' + i + 1)));
    }
    v.erase(v.begin() + 1);
    ASSERT_EQ(v.size(), n - 2);
    EXPECT_EQ(v[0], std::string(40, 'b'));
    EXPECT_EQ(v[1], std::string(40, 'd'));
  }
}
