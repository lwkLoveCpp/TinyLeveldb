#pragma once
#include <string>
using namespace std;
//抽象类，定义了迭代器的接口
class Iterator{ 
public:
    Iterator() = default;
    Iterator(const Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;
    virtual ~Iterator() = default;
    virtual bool Valid() const = 0;
    virtual void SeekToFirst() = 0;
    virtual void SeekToLast() = 0;
    virtual void Seek(const string& target) = 0;
    virtual void Next() = 0;
    virtual void Prev() = 0;
    virtual string key() const = 0;
    virtual string value() const = 0;
    virtual string status() const = 0;
};