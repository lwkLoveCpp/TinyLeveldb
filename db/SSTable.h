#include "block.h"
#include "filter_block.h"
#include "crc32c.h"
#include "coding.h"
static const size_t kBlockTrailerSize = 4;
//用于记录每个写入文件的块（data_block,index_block）的偏移量和大小
class BlockHandle {
    public:
     // Maximum encoding length of a BlockHandle
     enum { kMaxEncodedLength = 10 + 10 };
   
     // The offset of the block in the file.
     uint64_t offset() const { return offset_; }
     void set_offset(uint64_t offset) { offset_ = offset; }
   
     // The size of the stored block
     uint64_t size() const { return size_; }
     void set_size(uint64_t size) { size_ = size; }
   
     void EncodeTo(std::string* dst) const{
        coding::PutFixed64(dst,offset_);
        coding::PutFixed64(dst,size_);
     }
     Status DecodeFrom(slice* input){
        offset_ = coding::DecodeFixed64(input->data_);
        size_ = coding::DecodeFixed64(input->data_ + 8);
     }
   
    private:
     uint64_t offset_;
     uint64_t size_;
};
class TableBuilder{
    public:
    struct Rep{
        Rep(WritableFile* file,BloomFilterPolicy * filterPolicy):file(file){
            filter_block = new FilterBlockBuilder(filterPolicy);
            num_entries = 0;
            offset = 0;
        }
        WritableFile* file;
        uint64_t offset;//当前文件的写入偏移量。用于记录文件中下一个写入位置
        Status status;
        BlockBuilder data_block;
        BlockBuilder index_block;
        std::string last_key;
        int64_t num_entries;//记录已插入的键值对数量。
        bool closed;  // 标记 TableBuilder 是否已完成或被放弃。
        FilterBlockBuilder* filter_block;
      
        // 不变性：仅当 data_block 为空时，r->pending_index_entry 才为 true。
        bool pending_index_entry;//标记是否有尚未写入索引块（Index Block）的数据块（Data Block）。
        BlockHandle *pending_handle;  //用于存储上一个数据块的元信息（偏移量和大小）。
    };
    Rep *rep_;
    TableBuilder(WritableFile* file,BloomFilterPolicy* filterPolicy): rep_(new Rep(file,filterPolicy)) {
        if (rep_->filter_block != nullptr) {
            rep_->filter_block->StartBlock(0);
        }
    }
    Status Add(slice &key,slice &value){
        Rep *r = rep_;
        //每写完一个block就往index_block中添加索引信息
        if(r->pending_index_entry){
            string encodeHandle;
            r->pending_handle->EncodeTo(&encodeHandle);
            r->index_block.Add(rep_->last_key,encodeHandle);
            r->pending_index_entry = false;
        }
        r->filter_block->AddKey(key);

        r->last_key.assign(key.data(), key.size());
        r->num_entries++;
        r->data_block.Add(string(key.data()),string(value.data()));
        if(r->data_block.CurrentSizeEstimate() >= 1000){
            return Flush();
        }
        return OK;
    }
    //将未写入的数据写入file，并刷入磁盘
    Status Flush(){
        Rep* r = rep_;
        if(r->data_block.Empty()){
            return OK;
        }
        WriteBlock(r->data_block,r->pending_handle);
        r->pending_index_entry = true;
        r->file->FlushBUffer();
        r->filter_block->StartBlock(r->offset);
        return OK;
    }
    
    Status WriteBlock(BlockBuilder &block,BlockHandle *handle){
        Rep* r = rep_;
        //写入block数据
        //写入前调用Finish函数 将restarts数组和restartNum填入block
        slice block_contents = block.Finish();
        r->file->Append(block_contents);
        //设置该组block在sstable的偏移量和大小
        handle->set_offset(r->offset);
        handle->set_size(block_contents.size());
        //加上crc校验数据
        char trailer[kBlockTrailerSize];
        uint32_t crc = crc32c::Value(block_contents.data(), block_contents.size());
        crc = crc32c::Extend(crc, trailer, 1);  // Extend crc to cover block type
        coding::EncodeFixed32(trailer, crc32c::Mask(crc));
        Status s= r->file->Append(slice(trailer, kBlockTrailerSize));
        if(s!=OK){
            return s;
        }
        //更新偏移量
        r->offset += block_contents.size() + kBlockTrailerSize;
        r->data_block.Reset();
        return OK;
    }
    //sstable的收尾阶段，将index_block写入file，然后再加footer写入file
    Status Finish(){
        Rep *r = rep_;
        Flush();
        BlockHandle *indexHandle;
        if(r->pending_index_entry){
            std::string *handleCoding;
            r->pending_handle->EncodeTo(handleCoding);
            r->index_block.Add(r->last_key,*handleCoding);
            WriteBlock(r->index_block,indexHandle);
        }
        //加入footer信息：存储着index_block的元数据，index_block的偏移量和大小。这些数据在index_block写入文件后产生。
        string *indexCoding;
        indexHandle->EncodeTo(indexCoding);
        r->file->Append(slice(*indexCoding));
    }
};
