#include <iostream>
#include <string.h>
class String{
    public:
    int size_;
    int a[1];
    char* m_data;
    // int size() const{
    //     return size_;
    // }
    // char* c_str() const{
    //     return m_data;
    // }
    String(const char* data = nullptr){
        if(data){
            // size_ = strlen(data);
            m_data = new char[strlen(data)+1];
            memcpy(m_data, data,  strlen(data));
            m_data[ strlen(data)] = '\0';
        }else{
            m_data = nullptr;
            // size_ = 0;
        }
    }
};
// class base{
//     public:
//     virtual void test(){
//         std::cout<<"virtual"<<std::endl;
//     }
//     void temp(){
//         std::cout<<"??"<<std::endl;
//     }
//     int a = 2;
//     protected:
//     int b = 1;
//     private:
//     int c = 3;
// };
// class a: public base{
//     public:
//     void test() override{
//         std::cout<<base::a<<std::endl;
//         std::cout<<base::b<<std::endl;
//         base::temp();
//         std::cout<<"implement"<<std::endl;
//     }
    
//     void temp(){
//         std::cout<<base::a<<std::endl;
//     }
// };
class Node{
    public:
    Node(int a):data(a){}
    int data;
    Node* next;
};

int main(int, char**){
    Node* node1 = new Node(1);
    Node* node2 = new Node(2);
    Node* node3 = new Node(3);
    node1->next = node2;
    node2->next = node3;
    Node **ptr = &(node1->next);
    *ptr = node3;
    std::cout<<node1->next->data<<std::endl;
    // int* ptr = &node1->data;
    // *ptr = 5;
    // std::cout<<node1->data<<std::endl;
    return 0;
}
