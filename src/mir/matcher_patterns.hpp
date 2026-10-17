#pragma once

namespace foptim::fmir {

class RuleSet;

// all instruction selection rules, built once
const RuleSet &builtin_rules();
}  // namespace foptim::fmir
