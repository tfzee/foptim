#pragma once
#include <ankerl/unordered_dense.h>

#include <atomic>
#include <mutex>

#include "utils/stable_vec_ref.hpp"
#include "utils/types.hpp"

namespace foptim::fir {

constexpr u64 hash_mix(u64 h, u64 v) {
  const u128 p = static_cast<u128>(h ^ v) * 0x9E3779B97F4A7C15ULL;
  return static_cast<u64>(p) ^ static_cast<u64>(p >> 64);
}

// Thread safe hash consing table for the values of a StableVec. Sharded with a
// lock per shard and a per thread cache in front. Slots can be freed and
// reused, so every hit is checked by the caller and stale entries are replaced.
template <class T>
class InternTable {
  using Ref = utils::SRef<T>;
  static constexpr u32 NShards = 32;
  static constexpr u32 CacheSize = 256;

  struct IdentityHash {
    using is_avalanching = void;
    u64 operator()(u64 k) const noexcept { return k; }
  };
  struct alignas(64) Shard {
    std::mutex mtx;
    ankerl::unordered_dense::map<u64, Ref, IdentityHash> map;
  };
  struct CacheEntry {
    u64 table_id = 0;
    u64 hash = 0;
    Ref ref;
  };

  static inline std::atomic<u64> next_id{1};
  // never reused, so thread local entries of a dead table do not match
  const u64 id_ = next_id.fetch_add(1);
  Shard shards_[NShards];

  static CacheEntry &cache_entry(u64 hash) {
    thread_local CacheEntry cache[CacheSize];
    return cache[hash & (CacheSize - 1)];
  }

 public:
  // matches(ref): live and holds the value, make(): inserts it into the storage
  template <class Matches, class Make>
  Ref intern(u64 hash, Matches &&matches, Make &&make) {
    CacheEntry &ce = cache_entry(hash);
    if (ce.table_id == id_ && ce.hash == hash && matches(ce.ref)) {
      return ce.ref;
    }
    Shard &shard = shards_[(hash >> 40) % NShards];
    const std::lock_guard lock(shard.mtx);
    Ref *stale = nullptr;
    for (u64 key = hash;; key++) {
      auto it = shard.map.find(key);
      if (it == shard.map.end()) {
        Ref r = make();
        if (stale != nullptr) {
          *stale = r;
        } else {
          shard.map.emplace(key, r);
        }
        ce = {id_, hash, r};
        return r;
      }
      if (matches(it->second)) {
        ce = {id_, hash, it->second};
        return it->second;
      }
      if (stale == nullptr && !it->second.is_valid()) {
        stale = &it->second;
      }
    }
  }
};

}  // namespace foptim::fir
