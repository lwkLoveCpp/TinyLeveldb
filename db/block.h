#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "coding.h"
#include "iterator.h"
using namespace std;
static int groupSize = 20;

class Block{
public:
    Block(const Block&) = delete;       
    Block& operator=(const Block&) = delete;
    ~Block() = default;
    size_t get_size(){
        return this->size;
    }
    uint32_t restartNum(){
        return coding::DecodeFixed32(data+size-sizeof(uint32_t));
    }

private:
    char* data;
    size_t size;
    uint32_t result_offset;
};
class BlockBuilder{
public:
    BlockBuilder();
    BlockBuilder(const BlockBuilder&) = delete;
    BlockBuilder& operator=(const BlockBuilder&) = delete;
    ~BlockBuilder();
    void Add(const string& key, const string& value){
        //一组的首个元素
        if(counter ==0){
            restarts_.push_back(buffer.size());
            uint32_t shared = 0;
            uint32_t non_shared = key.size();
            uint32_t value_size = value.size();
            char buf[4];
            coding::EncodeFixed32(buf,shared);
            buffer.append(buf);
            coding::EncodeFixed32(buf,non_shared);
            buffer.append(buf);   
            coding::EncodeFixed32(buf,value_size);
            buffer.append(buf);
            buffer.append(key);
            buffer.append(value);
        }else{
            uint32_t shared = 0;
            int min_length = min(last_key.size(),key.size());
            while(shared<min_length){
                if(key[shared]!=last_key[shared]){
                    break;
                }
                shared++;
            }
            uint32_t non_shared = key.size()-shared;
            uint32_t value_size = value.size();

            char buf[4];
            coding::EncodeFixed32(buf,shared);
            buffer.append(buf);
            coding::EncodeFixed32(buf,non_shared);
            buffer.append(buf);   
            coding::EncodeFixed32(buf,value_size);
            buffer.append(buf);
            buffer.append(key.substr(shared));
            buffer.append(value);
        }
        last_key = key;
        counter++;
        if(counter >= groupSize){
            counter = 0;
        }   
    }
    //用于block中entry数量到达限定值后调用，给block的末尾加上restarts数组
    string Finish(){
        for(auto restart : restarts_){
            char buf[4];
            coding::EncodeFixed32(buf,restart);
            buffer.append(buf);
        }
        char buf[4];
        coding::EncodeFixed32(buf,restarts_.size());
        buffer.append(buf);
        finished = true;
        return buffer;

    }
    private:
    string last_key;
    string buffer;
    std::vector<uint32_t> restarts_;  
    int counter;
    bool finished = false;
};

//迭代器，用于遍历block中的entry
class blockIter : Iterator{
public:
    blockIter(char* data, uint32_t restarts_num):data(data),restarts_num(restarts_num),current(0),restart_index(restarts_num-1){
        restarts = coding::DecodeFixed32(data+size-restarts_num*sizeof(uint32_t)-sizeof(uint32_t));
        SeekToFirst();

    }
    blockIter(const blockIter&) = delete;
    blockIter& operator=(const blockIter&) = delete;
    ~blockIter(){

    }
    bool Valid() const{

    }

    void SeekToFirst(){
        current = coding::DecodeFixed32(data+restarts);
        Next();
    }
    void SeekToLast(){
        restart_index = restarts_num-1;
        //找到最后一个restarts数组的索引
        current = coding::DecodeFixed32(data+restarts+(restarts_num-1)*sizeof(uint32_t));
        char* limit = data+restarts;
        while(data+current<limit){
            Next();
        }
    }
    void Seek(const string& target){
        //二分查找
        int left=0;
        int right = restarts_num-1;
        while(left<=right){
            int mid = (left+right)/2;
            uint32_t offset = coding::DecodeFixed32(data+restarts+mid*sizeof(uint32_t));
            char* ptr = data+offset;
            uint32_t shared = coding::DecodeFixed32(ptr);
            uint32_t non_shared = coding::DecodeFixed32(ptr+sizeof(uint32_t));
            ptr += 3*sizeof(uint32_t);
            //每组的第一个元素没有共享键
            string key = string(ptr,non_shared);
            if(key<target){
                left = mid+1;
            }else{
                right = mid-1;
            }
        }
        //right是我们要找的restarts数组的索引
        restart_index = right;
        //找到了restarts数组的索引，接下来要找到target所在的键值对  
        current = coding::DecodeFixed32(data+restarts+right*sizeof(uint32_t));
        //在该起始位置开始遍历，直到找到大于等于target的键值对
        for(;;){
            Next();
            if(key_>=target){
                break;
            }
        }

    }
    void Next(){
        parseNext();
        current += key_.size()+value_.size()+3*sizeof(uint32_t);
    }
    void parseNext(){
        char* ptr = data+current;
        char* limit = data+restarts;
        if(ptr>=limit){
            return;
        }
        uint32_t shared = coding::DecodeFixed32(ptr);
        uint32_t non_shared = coding::DecodeFixed32(ptr+sizeof(uint32_t));
        uint32_t value_size = coding::DecodeFixed32(ptr+2*sizeof(uint32_t));
        ptr += 3*sizeof(uint32_t);
        key_.resize(shared);
        key_.append(ptr+shared,non_shared);
        value_ = string(ptr+non_shared,value_size);
        //更新restart_index
        while(restart_index+1<restarts_num){
            if(coding::DecodeFixed32(data+restarts+(restart_index+1)*sizeof(uint32_t))<current){
                restart_index++;
            }else{
                break;
            }
        }
    }
    // void Prev()
    // {
    //     string temp_key = key_;
    //     current= coding::DecodeFixed32(data+restarts+(restart_index-1)*sizeof(uint32_t));
    //     for(;;){
    //         parseNext();
    //         if(key_>=temp_key){
    //             break;
    //         }
    //     }


    // }
    string key() const{
        return key_;

    }
    string value() const{
        return value_;
    }
private:
    char* data;
    uint32_t current;//当前键值对在data中的偏移量
    uint32_t restarts_num;//restarts数组的大小
    uint32_t restart_index;//当前键值对所在restarts数组的索引
    uint32_t restarts;//restarts数组的首地址
    string key_;
    string value_;
    int size;
};