//该文件封装了关于文件系统操作的函数，包括文件的读写，文件的创建，删除，文件夹的创建，删除等操作。同时包括哦了线程的操作，包括线程的创建，销毁，线程的锁等操作。
#include "status.h"
#include <string>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#define MAX_BUFFER_SIZE 1024
#include <queue>
#include <thread>
#include <mutex>
#include <semaphore.h>
class slice{
public:
    const char* data_;
    size_t size_;
    slice():data_(nullptr),size_(0){}
    slice(const char* data,size_t size):data_(data),size_(size){}
      // Intentionally copyable.
    slice(const slice&) = default;
    slice& operator=(const slice&) = default;

    int size() const {return size_;}
    const char* data() const {
        return data_;
    }
    bool operator==(const slice& other) const {
        return ((size_ == other.size_) &&
                (memcmp(data_, other.data_, size_) == 0));
    }
      
    bool operator!=(const slice& other) const { return !(*this == other); }

};
class SequentialFile{
    public:
    SequentialFile(const std::string& fname, int fd)
    : filename_(std::move(fname)), fd(fd) { }
    ~SequentialFile() { close(fd); }
    Status Read(size_t n, slice result, char* scratch) {
        Status s;
        while(true){
            size_t read_size = read(fd,scratch,n);
            if(read_size<0){
                if(errno == EINTR){
                    continue;
                }
                s = IOError;
                break;
            }
            slice(scratch,read_size);
            s=OK;
            break;
        }
        return s;
    }
    Status Skip(uint64_t n) {
        if (lseek(fd, n, SEEK_CUR)) {
            return IOError;
        }
        return OK;
    }
    private:
    std::string filename_;
    int fd;
};
class RandomAccessFile{
public:
    RandomAccessFile(const std::string fimename,const int fd):filename(std::move(filename)),fd(fd){}
    ~RandomAccessFile(){
        close(fd);
    }
    Status Read(uint32_t offset,slice* result,char* scracth,int n){
        Status s;
        while(true){
            size_t read_size = pread(fd,scracth,n,offset);
            if(read_size<0){
                if(errno == EINTR){
                    continue;
                }
                s = IOError;
                break;
            }
            *result = slice(scracth,read_size);
            s=OK;
            break;
        }
        return s;
    }
private:
    int fd;
    std::string filename;
    bool has_permanent_fd;
};
class WritableFile{
    public:
    WritableFile(const std::string& fname,int fd):filename(std::move(fname)),fd(fd){
        getDirAndBase(filename);
        if(basename == "MANIFEST"){
            is_manifest = true;
        }
    }
    ~WritableFile(){
        close(fd);
    }
    Status Append(const slice& data){
        if(has_buffer_size + data.size()< MAX_BUFFER_SIZE){
            memcpy(buffer+has_buffer_size,data.data_,data.size());
            has_buffer_size+=data.size();
            return OK;
        }
        FlushBUffer();
        if(data.size() > MAX_BUFFER_SIZE){
            return WriteToFile(data);
        }
        memcpy(buffer,data.data_,data.size());
        has_buffer_size = data.size();
        return OK;
    }
    Status FlushBUffer(){
        if(has_buffer_size>0){
            Status s = WriteToFile(slice(buffer,has_buffer_size));
            has_buffer_size = 0;
            memset(buffer,0,MAX_BUFFER_SIZE);
            return s;
        }else{
            return IOError;
        }
    }
    Status WriteToFile(const slice& data){
        size_t offset = 0;
        while(offset < data.size()){
            size_t write_size = write(fd,data.data_+offset,data.size()-offset);
            if(write_size<0){
                if(errno == EINTR){
                    continue;
                }
                return IOError;
            }
            offset+=write_size;
            if(offset >= data.size()){
                break;
            }
        }
        return OK;
    }

    Status Fsync(){
        Status s = syncManifest();
        if(s == IOError){
            return s;
        }
        if(fsync(fd) == -1){
            return IOError;
        }
        return OK;
    }
private:
    Status syncManifest(){
        if(!is_manifest){
            return OK;
        }
        int dirfd = open(dirname.c_str(),O_DIRECTORY);
        if(dirfd == -1){
            return IOError;
        }
        if(fsync(dirfd) == -1){
            return IOError;
        }
        return OK;
    }
    bool syncfd(){
        if(fsync(fd) == -1){
            return false;
        }
        return true;
    }
    Status getDirAndBase(std::string filename){
        size_t pos = filename.rfind('/');
        if(pos == std::string::npos){
            basename = filename;
            dirname = ".";
            return OK;

        }           
        basename = filename.substr(pos+1,filename.size()-pos-1);
        dirname = filename.substr(0,pos);
        return OK;
    }
    
    std::string filename;
    int fd;
    char buffer[MAX_BUFFER_SIZE];
    int has_buffer_size;
    bool is_manifest;
    std::string dirname;
    std::string basename;
};
class env{
public:

    Status NewSequentialFile(const std::string& filename,
        SequentialFile** result) {
        int fd = ::open(filename.c_str(), O_RDONLY);
        if (fd < 0) {
            *result = nullptr;
            return IOError;
        }
        *result = new SequentialFile(filename, fd);
        return OK;
    }   

    Status NewRandomAccessFile(const std::string& filename,
            RandomAccessFile** result) {
    *result = nullptr;
    int fd = ::open(filename.c_str(), O_RDONLY );
        if (fd < 0) {
            return IOError;
        }
    *result = new RandomAccessFile(filename, fd);
    return OK;
    }

    Status NewWritableFile(const std::string& filename,
        WritableFile** result){
        int fd = ::open(filename.c_str(),
        O_TRUNC | O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            *result = nullptr;
            return IOError;
        }
        *result = new WritableFile(filename, fd);
        return OK;
    }
    Status NewAppendableFile(const std::string& filename,
        WritableFile** result) {
        int fd = ::open(filename.c_str(),
        O_APPEND | O_WRONLY | O_CREAT , 0644);
        if (fd < 0) {
            *result = nullptr;
            return IOError;
        }
        *result = new WritableFile(filename, fd);
        return OK;
    }
    bool FileExists(const std::string& filename) {
        return ::access(filename.c_str(), F_OK) == 0;
    }
    
    Status GetChildren(const std::string& directory_path,
                        std::vector<std::string>* result){
        result->clear();
        ::DIR* dir = ::opendir(directory_path.c_str());
        if (dir == nullptr) {
            return IOError;
        }
        struct ::dirent* entry;
        while ((entry = ::readdir(dir)) != nullptr) {
            result->emplace_back(entry->d_name);
        }
        ::closedir(dir);
        return OK;
    }
    
    Status RemoveFile(const std::string& filename) {
        if (::unlink(filename.c_str()) != 0) {
            return IOError;
        }
        return OK;
    }
    
    Status CreateDir(const std::string& dirname) {
        if (::mkdir(dirname.c_str(), 0755) != 0) {
            return IOError;
        }
        return OK;
    }

    Status RemoveDir(const std::string& dirname) {
        if (::rmdir(dirname.c_str()) != 0) {
            return IOError;
        }
        return OK;
    }
    
    Status GetFileSize(const std::string& filename, uint64_t* size) {
        struct ::stat file_stat;
        if (::stat(filename.c_str(), &file_stat) != 0) {
            *size = 0;
            return IOError;
        }
        *size = file_stat.st_size;
        return OK;
    }

    Status RenameFile(const std::string& from, const std::string& to) {
        if (std::rename(from.c_str(), to.c_str()) != 0) {
            return IOError;
        }
        return OK;
    }
//后台线程相关函数
    struct BackgroundWorkItem {
        explicit BackgroundWorkItem(void (*function)(void* arg), void* arg)
            : function(function), arg(arg) {}
    
        void (*const function)(void*);
        void* const arg;
    };
    bool is_background_thread_started = false;
    void BackgroundThreadMain(){
        while(true){
            
            sem_wait(&bg_queue_semaphore);
            std::lock_guard<std::mutex> lock(bg_queue_mutex);
            BackgroundWorkItem item = bg_queue.front();
            bg_queue.pop();
            item.function(item.arg);
        }
    };
    static void run(void * arg){
        env * e = (env*)arg;
        e->BackgroundThreadMain();
    }
    void Schedule(void (*function)(void* arg),void* arg){
        {
            std::lock_guard<std::mutex> lock(bg_queue_mutex);
            bg_queue.push(BackgroundWorkItem(function, arg));
            sem_post(&bg_queue_semaphore);
        }
        if(is_background_thread_started == false){
            is_background_thread_started = true;
            std::thread t(run,this);
            t.detach();
        }
    }
    std::mutex bg_queue_mutex; // 保护 bg_queue 的互斥锁
    std::queue<BackgroundWorkItem> bg_queue;
    sem_t bg_queue_semaphore;  // 信号量，用于通知后台线程有任务需要处理
    ~env(){
        sem_destroy(&bg_queue_semaphore);
    }

};