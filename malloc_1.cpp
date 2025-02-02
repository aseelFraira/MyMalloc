#include <unistd.h>
#define MAX_MEM 100000000
void* smalloc(size_t size){
    if(size <= 0 || size > MAX_MEM){
        return nullptr;
    }

    void* ptr = sbrk(size);

    if(*(int*)(ptr) == -1){
        return nullptr;
    }
    return ptr;
}