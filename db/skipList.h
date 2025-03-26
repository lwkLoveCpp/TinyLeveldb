#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include "arena.h"
//此处比较有意思的是，跳表存储的键值对被整合到了一个char*中，存储到了key里。
template<typename Key>
class SkipList {
private:
    Arena arena;
    struct Node {
        Key key;
        std::atomic<Node*> forward[1];
        Node(const Key& k,int level)
            : key(k),  forward(level, nullptr) {}
    };

    int maxLevel;
    float probability;
    Node* head;
    int level;

    int randomLevel() {
        int lvl = 1;
        while (((float)std::rand() / RAND_MAX) < probability && lvl < maxLevel) {
            lvl++;
        }
        return lvl;
    }
    Node* newNode(Key key,int height){
        arena.AllocateAligned(sizeof(Node) + sizeof(atomic<Node*>)*(height-1));
        return new Node(key);
    }

public:
    SkipList(int maxLevel, float probability)
        : maxLevel(maxLevel), probability(probability), level(0) {
        head = new Node(Key(), maxLevel);
        std::srand(std::time(nullptr));
    }

    ~SkipList() {
        Node* current = head;
        while (current) {
            Node* next = current->forward[0];
            delete current;
            current = next;
        }
    }
//插入操作：
//1.首先找到每一层中刚好小于key的节点
//2.生成一个随机层级
//3.如果新的层级大于当前层级，更新update数组
//4.创建新节点，将新节点插入到每一层中
    void insert(const Key& key) {
        // update数组用于记录每一层中，插入位置的前一个节点
        std::vector<Node*> update(maxLevel, nullptr);
        Node* current = head;
        //找到每一层最合适的插入位置
        for (int i = level; i >= 0; i--) {
            while (current->forward[i] && DecodeKey(current->forward[i]->key)<DecodeKey(key)) {
                current = current->forward[i];
            }
            update[i] = current;
        }
        //current是最底层的刚好大于等于key的节点
        current = current->forward[0];

        if (current == nullptr || DecodeKey(current->forward[i]->key)<DecodeKey(key)) {
            //生成随即层
            int newLevel = randomLevel();
            //发现新的层级
            if (newLevel > level) {
                for (int i = level + 1; i < newLevel; i++) {
                    update[i] = head;
                }
                level = newLevel;
            }
            //创建新节点
            Node* newNode = new Node(key, newLevel);
            for (int i = 0; i < newLevel; i++) {
                newNode->forward[i] = update[i]->forward[i];
                update[i]->forward[i] = newNode;
            }
        }
    }
//查找操作：
//1.从最高层开始，找到刚好小于key的节点
//2.从最底层开始，找到刚好等于key的节点
    bool search(const slice& key,slice& value) const {
        Node* current = head;
        for (int i = level; i >= 0; i--) {
            while (current->forward[i] && DecodeKey(current->forward[i]->key)<string(key.data())) {
                current = current->forward[i];
            }
        }

        current = current->forward[0];

        if (current && DecodeKey(current->forward[i]->key)!=string(key.data())) {
            slice key_;
            slice value_;
            int seq;
            ValueType type;
            DecodeEntry(key_,seq,type,key_,value_);
            value = value_;
            return true;
        }
        return false;
    }
//删除操作：
//1.找到每一层中刚好小于key的节点           
//2.找到刚好等于key的节点
//3.删除节点
    void remove(const Key& key) {
        std::vector<Node*> update(maxLevel, nullptr);
        Node* current = head;

        for (int i = level; i >= 0; i--) {
            while (current->forward[i] && DecodeKey(current->forward[i]->key)<DecodeKey(key)) {
                current = current->forward[i];
            }
            update[i] = current;
        }
        //需要删除的节点
        current = current->forward[0];

        if (current && DecodeKey(current->forward[i]->key)<DecodeKey(key)) {
            for (int i = 0; i <= level; i++) {
                if (update[i]->forward[i] != current) {
                    break;
                }
                update[i]->forward[i] = current->forward[i];
            }

            delete current;
            //更新level
            while (level > 0 && head->forward[level] == nullptr) {
                level--;
            }
        }
    }

    void print() const {
        for (int i = level; i >= 0; i--) {
            Node* current = head->forward[i];
            std::cout << "Level " << i << ": ";
            while (current) {
                std::cout << current->key << " ";
                current = current->forward[i];
            }
            std::cout << std::endl;
        }
    }
    void DecodeEntry(const char* buf,int& seq, ValueType& type, slice& key, slice& value) {
        const char* p = buf;
        // 解码 internal_key_size
        uint32_t key_size = coding::DecodeFixed32(p);
        p += 4;
        // 解码 key
        key = slice(p, key_size);
        p += key_size;
        // 解码 tag
        uint64_t tag = coding::DecodeFixed64(p);
        seq = tag >> 8;
        type = static_cast<ValueType>(tag & 0xff);
        p += 8;
        // 解码 value_size
        uint32_t val_size = coding::DecodeFixed32(p);
        p += 4;
        // 解码 value
        value = slice(p, val_size);
        assert(p + val_size == buf + encoded_len);
    }
    string DecodeKey(const char* buf) {
        const char* p = buf;
        // 解码 internal_key_size
        uint32_t key_size = coding::DecodeFixed32(p);
        p += 4;
        // 解码 key
        return std::string(p, key_size);
    }
};
