#pragma once
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>

#include <map>
#include <tuple>

#include "ir/access_meta.hpp"
#include "optim/analysis/mem/alias_sets.hpp"

namespace foptim {

/*
  Turns the tbaa, alias.scope and noalias metadata of the loads and stores of
  an LLVM module into sets of the module alias set table and an AccessMeta per
  access.

  A scalar type node is a set below its parent with disjoint siblings, the
  omnipotent char and every other child of the TBAA root are the top. A tag
  (base, access, offset) puts the access into the set of the access type and
  the field set of every struct on the way from the base down to the access
  type. Every scope is a set of its own, the scopes of one domain an access is
  in become one union.

  scan() creates all sets and runs before the function bodies are converted
  because the table is not thread safe, lookup() only reads.
*/
class AliasMetaReader {
 public:
  explicit AliasMetaReader(optim::AliasSetTable &tab) : tab(&tab) {}

  void scan(const llvm::Module &mod);
  [[nodiscard]] const fir::AccessMeta *lookup(const llvm::Instruction &i) const;

 private:
  using Key = std::tuple<const llvm::MDNode *, const llvm::MDNode *,
                         const llvm::MDNode *>;

  static Key key_of(const llvm::Instruction &i);
  void read(const Key &key);
  std::optional<optim::SetId> type_set(const llvm::MDNode *n, u32 depth);
  optim::SetId field_set(const llvm::MDNode *strukt, u64 offset);
  optim::SetId scope_set(const llvm::MDNode *scope);
  void read_tbaa(const llvm::MDNode *tag, fir::AccessMeta &out);
  void read_scopes(const llvm::MDNode *list, bool member, fir::AccessMeta &out);
  optim::SetId char_set();

  optim::AliasSetTable *tab;
  optim::SetId char_ = 0;
  std::map<const llvm::MDNode *, std::optional<optim::SetId>> types;
  std::map<std::pair<const llvm::MDNode *, u64>, optim::SetId> fields;
  std::map<const llvm::MDNode *, optim::SetId> groups;
  std::map<const llvm::MDNode *, optim::SetId> scopes;
  std::map<Key, fir::AccessMeta> cache;
};

}  // namespace foptim
