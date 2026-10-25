#include "llvm/alias_metadata.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>

#include <algorithm>

#include "utils/logging.hpp"
#include "utils/verbosity.hpp"

namespace foptim {

namespace {
constexpr u32 MaxTypeDepth = 32;

const llvm::MDNode *node_op(const llvm::MDNode *n, unsigned i) {
  if (i >= n->getNumOperands()) {
    return nullptr;
  }
  return llvm::dyn_cast_or_null<llvm::MDNode>(n->getOperand(i).get());
}

std::optional<u64> int_op(const llvm::MDNode *n, unsigned i) {
  if (i >= n->getNumOperands()) {
    return std::nullopt;
  }
  if (auto *c = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
          n->getOperand(i))) {
    return c->getZExtValue();
  }
  return std::nullopt;
}

bool is_string_node(const llvm::MDNode *n) {
  return n->getNumOperands() > 0 &&
         llvm::isa<llvm::MDString>(n->getOperand(0).get());
}

bool is_char_node(const llvm::MDNode *n) {
  auto *s = llvm::dyn_cast_or_null<llvm::MDString>(n->getOperand(0).get());
  return s != nullptr && s->getString() == "omnipotent char";
}

void unsupported(const char *what) {
  if (utils::verbose()) {
    fmt::println(stderr, "Warning: unsupported alias metadata: {}", what);
  }
}
}  // namespace

AliasMetaReader::Key AliasMetaReader::key_of(const llvm::Instruction &i) {
  return {i.getMetadata(llvm::LLVMContext::MD_tbaa),
          i.getMetadata(llvm::LLVMContext::MD_alias_scope),
          i.getMetadata(llvm::LLVMContext::MD_noalias)};
}

void AliasMetaReader::scan(const llvm::Module &mod) {
  for (const auto &f : mod) {
    for (const auto &bb : f) {
      for (const auto &i : bb) {
        if (!llvm::isa<llvm::LoadInst>(i) && !llvm::isa<llvm::StoreInst>(i)) {
          continue;
        }
        if (!i.hasMetadata()) {
          continue;
        }
        const Key k = key_of(i);
        if (std::get<0>(k) == nullptr && std::get<1>(k) == nullptr &&
            std::get<2>(k) == nullptr) {
          continue;
        }
        if (!cache.contains(k)) {
          read(k);
        }
      }
    }
  }
}

const fir::AccessMeta *AliasMetaReader::lookup(
    const llvm::Instruction &i) const {
  if (!i.hasMetadata()) {
    return nullptr;
  }
  auto it = cache.find(key_of(i));
  return it == cache.end() || it->second.empty() ? nullptr : &it->second;
}

void AliasMetaReader::read(const Key &key) {
  fir::AccessMeta m;
  if (std::get<0>(key) != nullptr) {
    read_tbaa(std::get<0>(key), m);
  }
  if (std::get<1>(key) != nullptr) {
    read_scopes(std::get<1>(key), true, m);
  }
  if (std::get<2>(key) != nullptr) {
    read_scopes(std::get<2>(key), false, m);
  }
  // contradicting scopes prove nothing
  for (auto s : m.scope_in) {
    const auto parts = tab->is_union(s) ? std::vector<optim::SetId>(
                                              tab->parts(s).begin(),
                                              tab->parts(s).end())
                                        : std::vector<optim::SetId>{s};
    for (auto leaf : parts) {
      if (std::ranges::find(m.not_in, leaf) != m.not_in.end()) {
        unsupported("access is in a scope it does not alias");
        m.scope_in.clear();
        m.not_in.clear();
        break;
      }
    }
    if (m.scope_in.empty()) {
      break;
    }
  }
  cache.emplace(key, std::move(m));
}

optim::SetId AliasMetaReader::char_set() {
  if (char_ == 0) {
    char_ = tab->create(std::span<const optim::SetId>{});
  }
  return char_;
}

std::optional<optim::SetId> AliasMetaReader::type_set(const llvm::MDNode *n,
                                                      u32 depth) {
  if (auto it = types.find(n); it != types.end()) {
    return it->second;
  }
  std::optional<optim::SetId> res;
  // scalar types are (name, parent, 0) or (name, parent) after LLVM upgraded
  // an old tag
  const bool scalar_shape =
      is_string_node(n) && node_op(n, 1) != nullptr &&
      (n->getNumOperands() == 2 ||
       (n->getNumOperands() == 3 && int_op(n, 2) == 0));
  if (depth < MaxTypeDepth && scalar_shape) {
    const llvm::MDNode *parent = node_op(n, 1);
    const bool parent_is_root =
        is_string_node(parent) && parent->getNumOperands() == 1;
    if (parent_is_root) {
      res = is_char_node(n) ? char_set() : tab->create_object(char_set());
    } else if (auto p = type_set(parent, depth + 1)) {
      res = tab->create_object(*p);
    }
  }
  types.emplace(n, res);
  return res;
}

optim::SetId AliasMetaReader::field_set(const llvm::MDNode *strukt,
                                        u64 offset) {
  auto key = std::make_pair(strukt, offset);
  if (auto it = fields.find(key); it != fields.end()) {
    return it->second;
  }
  auto g = groups.find(strukt);
  if (g == groups.end()) {
    g = groups.emplace(strukt, tab->create(std::span<const optim::SetId>{}))
            .first;
  }
  const optim::SetId s = tab->create_object(g->second);
  fields.emplace(key, s);
  return s;
}

void AliasMetaReader::read_tbaa(const llvm::MDNode *tag, fir::AccessMeta &out) {
  const llvm::MDNode *base = tag;
  const llvm::MDNode *access = tag;
  u64 offset = 0;
  if (!is_string_node(tag)) {
    base = node_op(tag, 0);
    access = node_op(tag, 1);
    auto off = int_op(tag, 2);
    if (base == nullptr || access == nullptr || !off) {
      unsupported("tbaa tag");
      return;
    }
    offset = *off;
  }
  auto access_set = type_set(access, 0);
  if (!access_set) {
    unsupported("tbaa access type is not a scalar");
    return;
  }
  IRVec<u32> fields_on_path;
  const llvm::MDNode *cur = base;
  for (u32 step = 0; cur != access; step++) {
    if (step >= MaxTypeDepth || !is_string_node(cur)) {
      unsupported("tbaa base type does not lead to the access type");
      return;
    }
    // the members are (type, offset) pairs behind the name
    const llvm::MDNode *best = nullptr;
    u64 best_off = 0;
    u32 n_members = 0;
    for (unsigned i = 1; i + 1 < cur->getNumOperands(); i += 2) {
      const llvm::MDNode *m = node_op(cur, i);
      auto mo = int_op(cur, i + 1);
      if (m == nullptr || !mo) {
        unsupported("tbaa struct node");
        return;
      }
      n_members++;
      if (*mo <= offset && (best == nullptr || *mo >= best_off)) {
        best = m;
        best_off = *mo;
      }
    }
    if (best == nullptr) {
      unsupported("tbaa offset outside of the struct");
      return;
    }
    if (n_members > 1) {
      fields_on_path.push_back(field_set(cur, best_off));
    }
    offset -= best_off;
    cur = best;
  }
  for (auto f : fields_on_path) {
    out.types.push_back(f);
  }
  out.types.push_back(*access_set);
}

optim::SetId AliasMetaReader::scope_set(const llvm::MDNode *scope) {
  auto it = scopes.find(scope);
  if (it == scopes.end()) {
    it = scopes.emplace(scope, tab->create(std::span<const optim::SetId>{}))
             .first;
  }
  return it->second;
}

void AliasMetaReader::read_scopes(const llvm::MDNode *list, bool member,
                                  fir::AccessMeta &out) {
  // scopes of one domain are alternatives
  std::map<const llvm::MDNode *, optim::SetId> per_domain;
  for (unsigned i = 0; i < list->getNumOperands(); i++) {
    const llvm::MDNode *scope = node_op(list, i);
    if (scope == nullptr || node_op(scope, 1) == nullptr) {
      unsupported("scope node");
      continue;
    }
    const optim::SetId s = scope_set(scope);
    if (!member) {
      if (std::ranges::find(out.not_in, s) == out.not_in.end()) {
        out.not_in.push_back(s);
      }
      continue;
    }
    const llvm::MDNode *domain = node_op(scope, 1);
    auto d = per_domain.find(domain);
    if (d == per_domain.end()) {
      per_domain.emplace(domain, s);
    } else {
      d->second = tab->union_of(d->second, s);
    }
  }
  for (const auto &[domain, s] : per_domain) {
    out.scope_in.push_back(s);
  }
}

}  // namespace foptim
