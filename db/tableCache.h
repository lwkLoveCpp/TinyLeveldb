#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "env.h"
#include "coding.h"
// LRU缓存实现

struct LRUHandle {
  void* value;
  void (*deleter)(const slice&, void* value);
  LRUHandle* next_hash;
  LRUHandle* next;
  LRUHandle* prev;
  size_t charge;  // TODO(opt): 是否只允许使用uint32_t？
  size_t key_length;
  bool in_cache;     // 条目是否在缓存中。
  uint32_t refs;     // 引用计数，包括缓存引用（如果存在）。
  uint32_t hash;     // 键的哈希值；用于快速分片和比较
  char key_data[1];  // 键的起始部分

  slice key() const {
    // 如果LRU句柄是空链表的头节点，则next等于this。
    // 链表头节点从不具有有效的键。
    assert(next != this);

    return slice(key_data, key_length);
  }
};

class HandleTable {
 public:
  HandleTable() : length_(0), elems_(0), list_(nullptr) { Resize(); }
  ~HandleTable() { delete[] list_; }

  LRUHandle* Lookup(const slice& key, uint32_t hash) {
    return FindPointer(key, hash)->next_hash;
  }

  LRUHandle* Insert(LRUHandle* h) {
    LRUHandle* pre = FindPointer(h->key(), h->hash);
    LRUHandle* old = pre->next_hash;
    h->next_hash = (old == nullptr ? nullptr : old->next_hash);
    pre->next_hash = h;
    if (old == nullptr) {
      ++elems_;
      if (elems_ > length_) {
        // 由于每个缓存条目相对较大，我们的目标是保持较小的平均链表长度（<= 1）。
        Resize();
      }
    }
    return old;
  }

  LRUHandle* Remove(const slice& key, uint32_t hash) {
    LRUHandle* pre = FindPointer(key, hash);
    LRUHandle* result = pre->next_hash;
    if(result == nullptr){
      return pre->next_hash;
    }
    pre->next_hash = result->next_hash;
    --elems_;
    return result;
  }

 private:
  uint32_t length_;
  uint32_t elems_;
  LRUHandle** list_;


  // 此处有个细节需要注意：返回的ptr是节点里的next指针的指针
  LRUHandle* FindPointer(const slice& key, uint32_t hash) {
    LRUHandle** ptr = &list_[hash & (length_ - 1)];
    LRUHandle *start = *ptr;
    while(start->next_hash != nullptr && 
      (start->next_hash->hash != hash || key != (start->next_hash)->key())){
      start = start->next_hash;
    }
    return start;
  }

  void Resize() {
    uint32_t new_length = 4;
    while (new_length < elems_) {
      new_length *= 2;
    }
    LRUHandle** new_list = new LRUHandle*[new_length];
    memset(new_list, 0, sizeof(new_list[0]) * new_length);
    uint32_t count = 0;
    for (uint32_t i = 0; i < length_; i++) {
      LRUHandle* h = list_[i];
      while (h->next_hash!= nullptr) {
        LRUHandle* node = h->next_hash;
        h->next_hash = node->next_hash;
        uint32_t hash = node->hash;
        LRUHandle* ptr = new_list[hash & (new_length - 1)];
        node->next_hash = ptr->next;
        ptr->next_hash = node;
        count++;
      }
    }
    assert(elems_ == count);
    delete[] list_;
    list_ = new_list;
    length_ = new_length;
  }
};

// 单个分片的LRU缓存。
class LRUCache {
 public:
  LRUCache();
  ~LRUCache();

  // 与构造函数分开，以便调用者可以轻松创建LRUCache数组
  void SetCapacity(size_t capacity) { capacity_ = capacity; }

  // 类似于Cache方法，但带有额外的“hash”参数。
  LRUHandle* Insert(const slice& key, uint32_t hash, void* value,
                        size_t charge,
                        void (*deleter)(const slice& key, void* value));
  LRUHandle* Lookup(const slice& key, uint32_t hash);
  void Release(LRUHandle* handle);
  void Erase(const slice& key, uint32_t hash);
  void Prune();// 清理未被引用的条目：
  size_t TotalCharge() const {
    // MutexLock l(&mutex_);
    return usage_;
  }

 private:
  void LRU_Remove(LRUHandle* e);
  void LRU_Append(LRUHandle* list, LRUHandle* e);
  void Ref(LRUHandle* e);
  void Unref(LRUHandle* e);
  bool FinishErase(LRUHandle* e) ;
  // EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // 在使用前初始化。
  size_t capacity_;

  // mutex_ 保护以下状态。
  // mutable port::Mutex mutex_;
  size_t usage_ ;
  //GUARDED_BY(mutex_);

  // LRU链表的虚拟头节点。
  // lru.prev是最新的条目，lru.next是最旧的条目。
  // 条目具有refs==1且in_cache==true。
  LRUHandle lru_ ;
  // GUARDED_BY(mutex_);

  // 使用中的链表的虚拟头节点。
  // 条目正在被客户端使用，具有refs >= 2且in_cache==true。
  LRUHandle in_use_;
  //  GUARDED_BY(mutex_);

  HandleTable table_;
  //  GUARDED_BY(mutex_);
};

LRUCache::LRUCache() : capacity_(0), usage_(0) {
  // 创建空的循环链表。
  lru_.next = &lru_;
  lru_.prev = &lru_;
  in_use_.next = &in_use_;
  in_use_.prev = &in_use_;
}

LRUCache::~LRUCache() {
  assert(in_use_.next == &in_use_);  // 如果调用者有未释放的句柄，则报错
  for (LRUHandle* e = lru_.next; e != &lru_;) {
    LRUHandle* next = e->next;
    assert(e->in_cache);
    e->in_cache = false;
    assert(e->refs == 1);  // lru_链表的不变量。
    Unref(e);
    e = next;
  }
}

void LRUCache::Ref(LRUHandle* e) {
  if (e->refs == 1 && e->in_cache) {  // 如果在lru_链表中，则移动到in_use_链表。
    LRU_Remove(e);
    LRU_Append(&in_use_, e);
  }
  e->refs++;
}

void LRUCache::Unref(LRUHandle* e) {
  assert(e->refs > 0);
  e->refs--;
  if (e->refs == 0) {  // 释放。
    assert(!e->in_cache);
    (*e->deleter)(e->key(), e->value);
    free(e);
  } else if (e->in_cache && e->refs == 1) {
    // 不再使用；移动到lru_链表。
    LRU_Remove(e);
    LRU_Append(&lru_, e);
  }
}

void LRUCache::LRU_Remove(LRUHandle* e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
}

void LRUCache::LRU_Append(LRUHandle* list, LRUHandle* e) {
  // 通过插入到*list之前，使“e”成为最新的条目
  e->next = list;
  e->prev = list->prev;
  e->prev->next = e;
  e->next->prev = e;
}

LRUHandle* LRUCache::Lookup(const slice& key, uint32_t hash) {
  // MutexLock l(&mutex_);
  LRUHandle* e = table_.Lookup(key, hash);
  if (e != nullptr) {
    Ref(e);
  }
  return e;
}

void LRUCache::Release(LRUHandle* handle) {
  // MutexLock l(&mutex_);
  Unref(reinterpret_cast<LRUHandle*>(handle));
}

LRUHandle* LRUCache::Insert(const slice& key, uint32_t hash, void* value,
                                size_t charge,
                                void (*deleter)(const slice& key,
                                                void* value)) {
  // MutexLock l(&mutex_);

  LRUHandle* e =
      reinterpret_cast<LRUHandle*>(malloc(sizeof(LRUHandle) - 1 + key.size()));
  e->value = value;
  e->deleter = deleter;
  e->charge = charge;
  e->key_length = key.size();
  e->hash = hash;
  e->in_cache = false;
  e->refs = 1;  // 用于返回的句柄。
  memcpy(e->key_data, key.data(), key.size());

  if (capacity_ > 0) {
    e->refs++;  // 用于缓存的引用。
    e->in_cache = true;
    LRU_Append(&in_use_, e);
    usage_ += charge;
    FinishErase(table_.Insert(e));
  } else {  // 不缓存。（支持capacity_==0并关闭缓存。）
    // next由key()中的断言读取，因此必须初始化
    e->next = nullptr;
  }
  while (usage_ > capacity_ && lru_.next != &lru_) {
    LRUHandle* old = lru_.next;
    assert(old->refs == 1);
    bool erased = FinishErase(table_.Remove(old->key(), old->hash));
    if (!erased) {  // 避免在编译NDEBUG时未使用变量
      assert(erased);
    }
  }

  return reinterpret_cast<LRUHandle*>(e);
}

// 如果e != nullptr，完成从缓存中移除*e的操作；它已经从哈希表中移除。
// 返回e是否不为nullptr。
bool LRUCache::FinishErase(LRUHandle* e) {
  if (e != nullptr) {
    assert(e->in_cache);
    LRU_Remove(e);
    e->in_cache = false;
    usage_ -= e->charge;
    Unref(e);
  }
  return e != nullptr;
}

void LRUCache::Erase(const slice& key, uint32_t hash) {
  // MutexLock l(&mutex_);
  FinishErase(table_.Remove(key, hash));
}

void LRUCache::Prune() {
  // MutexLock l(&mutex_);
  while (lru_.next != &lru_) {
    LRUHandle* e = lru_.next;
    assert(e->refs == 1);
    bool erased = FinishErase(table_.Remove(e->key(), e->hash));
    if (!erased) {  // 避免在编译NDEBUG时未使用变量
      assert(erased);
    }
  }
}

static const int kNumShardBits = 4;
static const int kNumShards = 1 << kNumShardBits;

class ShardedLRUCache {
 private:
  LRUCache shard_[kNumShards];
  // port::Mutex id_mutex_;
  uint64_t last_id_;

  static inline uint32_t HashSlice(const slice& s) {
    return Hash(s.data_, s.size(), 0);
  }

  static uint32_t Shard(uint32_t hash) { return hash >> (32 - kNumShardBits); }

 public:
  explicit ShardedLRUCache(size_t capacity) : last_id_(0) {
    const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
    for (int s = 0; s < kNumShards; s++) {
      shard_[s].SetCapacity(per_shard);
    }
  }
  ~ShardedLRUCache(){}
  LRUHandle* Insert(const slice& key, void* value, size_t charge,
                 void (*deleter)(const slice& key, void* value))  {
    const uint32_t hash = HashSlice(key);
    return shard_[Shard(hash)].Insert(key, hash, value, charge, deleter);
  }
  LRUHandle* Lookup(const slice& key) {
    const uint32_t hash = HashSlice(key);
    return shard_[Shard(hash)].Lookup(key, hash);
  }
  void Release(LRUHandle* handle) {
    LRUHandle* h = (handle);
    shard_[Shard(h->hash)].Release(handle);
  }
  void Erase(const slice& key) {
    const uint32_t hash = HashSlice(key);
    shard_[Shard(hash)].Erase(key, hash);
  }
  void* Value(LRUHandle* handle) {
    return (handle)->value;
  }
  uint64_t NewId() {
    // MutexLock l(&id_mutex_);
    return ++(last_id_);
  }
  void Prune() {
    for (int s = 0; s < kNumShards; s++) {
      shard_[s].Prune();
    }
  }
  size_t TotalCharge() const {
    size_t total = 0;
    for (int s = 0; s < kNumShards; s++) {
      total += shard_[s].TotalCharge();
    }
    return total;
  }
};


ShardedLRUCache* NewLRUCache(size_t capacity) { return new ShardedLRUCache(capacity); }

uint32_t Hash(const char* data, size_t n, uint32_t seed) {
  // 类似于murmur hash
  const uint32_t m = 0xc6a4a793;
  const uint32_t r = 24;
  const char* limit = data + n;
  uint32_t h = seed ^ (n * m);

  // 每次处理四个字节
  while (limit - data >= 4) {
    uint32_t w = coding::DecodeFixed32(data);
    data += 4;
    h += w;
    h *= m;
    h ^= (h >> 16);
  }

  // 处理剩余的字节
  switch (limit - data) {
    case 3:
      h += static_cast<uint8_t>(data[2]) << 16;
      // FALLTHROUGH_INTENDED;
    case 2:
      h += static_cast<uint8_t>(data[1]) << 8;
      // FALLTHROUGH_INTENDED;
    case 1:
      h += static_cast<uint8_t>(data[0]);
      h *= m;
      h ^= (h >> r);
      break;
  }
  return h;
}