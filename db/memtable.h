#include <cassert>
#include <stddef.h>
#include <cstring>
#include "env.h"
#include "arena.h"
#include "skipList.h"
#include "coding.h"
enum ValueType { kTypeDeletion = 0x0, kTypeValue = 0x1 };
class MemTable {
    public:
     // MemTables are reference counted.  The initial reference count
     // is zero and the caller must call Ref() at least once.
     explicit MemTable();
   
     MemTable(const MemTable&) = delete;
     MemTable& operator=(const MemTable&) = delete;
   
     // Increase reference count.
     void Ref() { ++refs_; }
   
     // Drop reference count.  Delete if no more references exist.
     void Unref() {
       --refs_;
       assert(refs_ >= 0);
       if (refs_ <= 0) {
         delete this;
       }
     }
   
     // Returns an estimate of the number of bytes of data in use by this
     // data structure. It is safe to call when MemTable is being modified.
     size_t ApproximateMemoryUsage(){
          return arena_.MemoryUsage();
     }
   
     // Add an entry into memtable that maps key to value at the
     // specified sequence number and with the specified type.
     // Typically value will be empty if type==kTypeDeletion.
     void Add(int seq, ValueType type, const slice& key,const slice& value){
            size_t key_size = key.size();
            size_t val_size = value.size();
            // size_t internal_key_size = key_size + 8;
            const size_t encoded_len = key_size+val_size+8+4+4;
            char* buf = arena_.Allocate(encoded_len);
            //存放internal_key_size
            coding::EncodeFixed32(buf, key_size);
            auto p = buf+4;
            //存放key
            std::memcpy(p, key.data_, key_size);
            p += key_size;
            //存放tag
            coding::EncodeFixed64(p, (seq << 8) | type);
            p += 8;
            //存放value_size
            coding::EncodeFixed32(p, val_size);
            p+=4;
            //存放value
            std::memcpy(p, value.data_, val_size);
            assert(p + val_size == buf + encoded_len);
            table_.insert(slice(buf,encoded_len));
      }
      Status Get(slice key,slice &value){
        bool a = table_.search(key,value);
        if(a){
            return OK;
        }
        return IOError;
      }
   
     // If memtable contains a value for key, store it in *value and return true.
     // If memtable contains a deletion for key, store a NotFound() error
     // in *status and return true.
     // Else, return false.
    //  bool Get(const LookupKey& key, std::string* value, Status* s);
     // Increase reference count.
     ~MemTable(){ assert(refs_ == 0); }  
     // Private since only Unref() should be used to delete it
     int refs_;
     Arena arena_;
     SkipList<slice> table_;
   };