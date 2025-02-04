#include <unistd.h>
#include <string.h>
#include <cmath>
#include <sys/mman.h>
#include <iostream>


#define MAX_MEM 100000000
#define MAX_ORDER 10
#define MAX_BLOCK_SIZE (128 * 1024)
#define NUM_BLOCKS 32

// Metadata structure for each memory block
struct MallocMetadata {
    size_t m_data_size; //Only the user data
    size_t m_size; //The size of the block with the meta data
    bool m_is_free;
    MallocMetadata* m_next;
    MallocMetadata* m_prev;
};

// Doubly linked list to manage blocks
struct list{
    MallocMetadata* m_head;
    MallocMetadata* m_tail;
    size_t m_size;
    void insert(MallocMetadata* m);
    void remove(MallocMetadata* m);
};

// Inserting a block into the list (ordered by memory address)
void list::insert(MallocMetadata* m) {
    if (m_size == 0) {
        m_head = m;
        m_tail = m;
    }
    if ((m) < (m_head)) {
        m->m_next = m_head;
        m_head->m_prev = m;
        m_head = m;
    } else if ((m) > (m_tail)) {
        m_tail->m_next = m;
        m->m_prev = m_tail;
        m_tail = m;
    } else {
        MallocMetadata* current = m_head;
        while (current && current->m_next) {
            if ((m) >= (current) &&
                (m) <= (current->m_next)) {
                m->m_next = current->m_next;
                m->m_next->m_prev = m;
                m->m_prev = current;
                current->m_next = m;
                break;
            }
            current = current->m_next;
        }
    }
    m_size++;

}

// Removing a block from the list
void list::remove(MallocMetadata *m) {
    if(m_size == 0){
        return;
    }
    if(m_size == 1){
        m_head = nullptr;
        m_tail = nullptr;
    }
    else if(m_tail == m){
        m_tail = m->m_prev;
        m_tail->m_next = nullptr;
        m->m_prev = nullptr;
    }
    else if(m_head == m){
        m_head = m->m_next;
        m_head->m_prev = nullptr;
        m->m_next = nullptr;
    }else{
        m->m_prev->m_next = m->m_next;
        m->m_next->m_prev = m->m_prev;
        m->m_next = nullptr;
        m->m_prev = nullptr;
    }
    m_size--;


}

class Heap{
private:
    size_t _blocks_num;
    size_t _free_blocks_num;
    size_t _free_blocks_bytes;
    size_t _all_bytes;
    list _allocated_blocks[MAX_ORDER + 1];
    list _free_blocks[MAX_ORDER + 1];
    bool _is_first_time;

    int _get_order(size_t size) const;
    MallocMetadata* _getMetaDataPtr(void* ptr) const;
    MallocMetadata* _get_best_fit_block(int order);
    void _div_buddies(int order);
    void _merge_buddies(size_t order);

public:
    void _init();
    Heap():_blocks_num(0),_free_blocks_num(0),_free_blocks_bytes(0),_all_bytes(0),_is_first_time(true){}
    size_t _get_blocks_num() const;
    size_t _get_allocated_blocks_bytes() const;
    size_t _get_free_blocks_num() const;
    size_t _get_free_blocks_bytes() const;
    size_t _get_Metadata_size() const;
    size_t _get_block_size(void* p) const;
    size_t _get_all_bytes() const;


    void* _alloc_block(size_t size);
    void _free_block(void* p);
    bool _check_merge(void* oldp, size_t size);
    void* _merge_blocks_if_needed(void* oldp, size_t size);
};

int Heap::_get_order(size_t size) const {
    size_t needed_size = static_cast<size_t>(ceil((size) / 128.0));
    int order = std::ceil(std::log2(std::ceil(needed_size)));
    return order;
}
MallocMetadata *Heap::_getMetaDataPtr(void *ptr) const {
    if(!ptr){
        return nullptr;
    }
    return (MallocMetadata *)((char *)ptr - sizeof(MallocMetadata));
}

size_t Heap::_get_all_bytes() const {
    return _all_bytes;
}
void Heap::_init() {
    if (!_is_first_time) {
        return;
    }
    _is_first_time = false;

    void* heap_break = sbrk(0);   // Get the current program break
    size_t chunk_size = MAX_BLOCK_SIZE * NUM_BLOCKS; // 32 * 128 KB = 4 MB
    intptr_t break_address = (intptr_t)(heap_break);

    // Align the program break to the next multiple of chunk_size (4 MB)
    intptr_t alignment = (break_address + chunk_size - 1) & ~(chunk_size - 1);
    size_t alignment_offset = alignment - break_address;

    // Adjust the program break to ensure alignment and allocate memory
    void* newPtr = sbrk(chunk_size + alignment_offset);
    if (!newPtr) {
        return;
    }

    // Adjust newPtr to the aligned starting address
    newPtr = (char*)newPtr + alignment_offset;

    // Initialize free and allocated block lists
    for (int i = 0; i <= MAX_ORDER; i++) {
        _free_blocks[i] = {nullptr, nullptr, 0};
        _allocated_blocks[i] = {nullptr, nullptr, 0};
    }

    // Initialize metadata for the 32 blocks and add them to the free list of MAX_ORDER
    for (int i = 0; i < NUM_BLOCKS; i++) {
        void* blockStart = (char*)newPtr + i * MAX_BLOCK_SIZE;
        MallocMetadata* newMeta = reinterpret_cast<MallocMetadata*>(blockStart);

        // Initialize metadata for each block
        newMeta->m_is_free = true;
        newMeta->m_data_size = MAX_BLOCK_SIZE - _get_Metadata_size(); // Usable size
        newMeta->m_size = MAX_BLOCK_SIZE;                              // Total size
        newMeta->m_next = nullptr;
        newMeta->m_prev = nullptr;

        // Insert block into the free list of MAX_ORDER
        _free_blocks[MAX_ORDER].insert(newMeta);
    }

    // Update heap statistics
    _free_blocks_num = NUM_BLOCKS;
    _free_blocks_bytes = MAX_BLOCK_SIZE * NUM_BLOCKS - _get_Metadata_size() * NUM_BLOCKS;
    _all_bytes = _free_blocks_bytes;
    _blocks_num = NUM_BLOCKS;

}


size_t Heap::_get_blocks_num() const {
    return _blocks_num;
}

size_t Heap::_get_free_blocks_num() const {
    return _free_blocks_num;
}

size_t Heap::_get_free_blocks_bytes() const {
    return _free_blocks_bytes;
}

size_t Heap::_get_Metadata_size() const {
    return sizeof(MallocMetadata);
}

void Heap::_div_buddies(int order) {
    int free_order = -1;
    for(int i = order + 1;i <=MAX_ORDER; i++){
        if(_free_blocks[i].m_size > 0){
            free_order = i;
            break;
        }
    }
    if(free_order == -1){
        return;
    }
    for(int j = free_order;j > order; j--){
        MallocMetadata *temp = _free_blocks[j].m_head;
        MallocMetadata *buddy1, *buddy2;

        buddy1 = temp;
        size_t blockSize = (temp->m_data_size - _get_Metadata_size());


        buddy1->m_data_size = blockSize / 2;
        buddy1->m_is_free = true;
        buddy1->m_size = (temp->m_size) / 2;

        buddy2 = (MallocMetadata*)((char*)temp + (buddy1->m_size));
        buddy2->m_is_free = true;
        buddy2->m_data_size = buddy1->m_data_size;
        buddy2->m_size = buddy1->m_size;

        // divide the block to two each are the same size.
        _free_blocks[j].remove(temp);
        _free_blocks[j - 1].insert(buddy1);
        _free_blocks[j - 1].insert(buddy2);

        _blocks_num++;
        _free_blocks_num++;
        _free_blocks_bytes -= _get_Metadata_size();
        _all_bytes -= _get_Metadata_size();
    }

}

MallocMetadata *Heap::_get_best_fit_block(int order) {
    if (_free_blocks[order].m_size == 0) {
        if (order == MAX_ORDER) {
            return nullptr;
        }
        _div_buddies(order);
        if (_free_blocks[order].m_size == 0) {
            return nullptr; // No block available even after splitting
        }
    }

    MallocMetadata *res = _free_blocks[order].m_head;
    if (!res) {
        return nullptr;
    }

    _free_blocks[order].remove(res);
    _allocated_blocks[order].insert(res);
    _free_blocks_bytes -= res->m_data_size;

    res->m_is_free = false;
    return res;
}

void* Heap::_alloc_block(size_t size) {
    int ord = _get_order(size + sizeof(MallocMetadata));

    if (ord > MAX_ORDER) {
        void *ptr = mmap(nullptr, size + sizeof(MallocMetadata),
                         PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (ptr == (void *) -1) {
            return nullptr;
        }

        MallocMetadata *newBlock = (MallocMetadata *) ptr;
        newBlock->m_data_size = size;
        newBlock->m_size = size + _get_Metadata_size();
        newBlock->m_next = nullptr;
        newBlock->m_prev = nullptr;
        newBlock->m_is_free = false;

        _blocks_num++;
        _all_bytes += newBlock->m_data_size;

        return newBlock;

    } else {
        MallocMetadata *ptr = _get_best_fit_block(ord);
        if (ptr) {
            _free_blocks_num--;
            return ptr;
        }
        return nullptr;
    }
}

void Heap::_free_block(void* p) {
    MallocMetadata* temp = _getMetaDataPtr(p);
    if (!temp) {
        return;
    }
    if (temp->m_is_free) {
        return;
    }
    temp->m_is_free = true;
    size_t order = _get_order(temp->m_size);
    if (temp->m_size > MAX_BLOCK_SIZE)
    {
        size_t size = temp->m_size;
        munmap(temp, size);
        _blocks_num--;
        _all_bytes -= (size - _get_Metadata_size());
    }
    else
    {

        _allocated_blocks[order].remove(temp);
        _free_blocks[order].insert(temp);
        _free_blocks_num++;
        _free_blocks_bytes += temp->m_data_size;
        _merge_buddies(order);
    }
}
void Heap::_merge_buddies(size_t order) {
    if(order == MAX_ORDER){
        return;
    }
    MallocMetadata* currBlock = _free_blocks[order].m_head;
    if(!currBlock){ //i think its rudnadnt
        return;
    }
    while(currBlock && currBlock->m_next){
        intptr_t buddy_address = reinterpret_cast<intptr_t>(currBlock) ^ currBlock->m_size;
        if (buddy_address == reinterpret_cast<intptr_t>(currBlock->m_next)){

            MallocMetadata* nextBlock = currBlock->m_next;
            _free_blocks[order].remove(currBlock);
            _free_blocks[order].remove(nextBlock);
            _free_blocks[order + 1].insert(currBlock);

            currBlock->m_data_size += nextBlock->m_size;
            currBlock->m_size += nextBlock->m_size;

            _blocks_num--;
            _free_blocks_num--;
            _free_blocks_bytes += _get_Metadata_size();
            _all_bytes += _get_Metadata_size();
            _merge_buddies(order + 1);
            return;

        }
        currBlock = currBlock->m_next;
    }
}
size_t Heap::_get_block_size(void* p) const {
    MallocMetadata* curr = _getMetaDataPtr(p);
    if(curr){
        return curr->m_data_size;
    }
    return -1;
}

bool Heap::_check_merge(void *oldp, size_t size) {
    MallocMetadata* p = _getMetaDataPtr(oldp);
    size_t curr_size = p->m_size;

    while (true) {
        // Compute the next block by moving `curr_size + metadata_size` forward
        MallocMetadata* next_block = reinterpret_cast<MallocMetadata*>(
                reinterpret_cast<intptr_t>(p) ^ p->m_size);

        // Ensure the next block is valid and free before merging
        if (!next_block || !next_block->m_is_free || curr_size >= (size+_get_Metadata_size())) {
            break;
        }
        curr_size += next_block->m_size;
        p = next_block;                     // Move pointer to the merged block


    }

    return curr_size >= (size + _get_Metadata_size());
}
void* Heap::_merge_blocks_if_needed(void *oldp, size_t size) {
    MallocMetadata* p = _getMetaDataPtr(oldp);
    size_t curr_size = p->m_size;

    int remove_index = _get_order(p->m_size);
    _allocated_blocks[remove_index].remove(p);

    while (true) {
        // Compute the next block location
        MallocMetadata* next_block = (MallocMetadata*)(reinterpret_cast<intptr_t>(p) xor p->m_size);

        // Stop merging if:
        // 1. The next block is not valid.
        // 2. The next block is not free.
        // 3. The total merged size already reaches/exceeds the required `size`.
        if (!next_block || !next_block->m_is_free || curr_size >= (size + _get_Metadata_size())) {
            break;
        }

        int order = _get_order(next_block->m_size);
        _free_blocks[order].remove(next_block);
        curr_size += next_block->m_size;

        _blocks_num--;
        _free_blocks_num--;
        _all_bytes += _get_Metadata_size();
        _free_blocks_bytes -= next_block->m_data_size;

        next_block->m_is_free = false;  // Mark the merged block as used (prevent reuse)

        // Ensure the leftmost block (earlier address) is used as the merged block
        if (reinterpret_cast<intptr_t>(next_block) < reinterpret_cast<intptr_t>(p)) {
            p = next_block;
        }
    }

    // Update the metadata of the new merged block
    p->m_size = curr_size;
    p->m_data_size = curr_size - _get_Metadata_size();
    int index = _get_order(curr_size);
    _allocated_blocks[index].insert(p);

    // Return the newly merged block pointer
    return (char*)p + _get_Metadata_size();
}

Heap heap;

void* smalloc(size_t size){
    heap._init();
    if (size <= 0 || size > MAX_MEM)
    {
        return nullptr;
    }
    void *ptr = heap._alloc_block(size);

    return (!ptr) ? nullptr : (char *)ptr + heap._get_Metadata_size();

}
void *scalloc(size_t num, size_t size)
{
    void *res = smalloc(num * size);

    if (res == nullptr)
    {
        return nullptr;
    }

    memset(res, 0, num * size);

    return res;
}

void sfree(void *ptr)
{
    if (ptr == nullptr)
    {
        return;
    }
    heap._free_block(ptr);
}

void *srealloc(void *oldp, size_t size)
{

    if (size <= 0 || size > MAX_MEM)
    {
        return nullptr;
    }

    if (oldp == nullptr)
    {
        return smalloc(size);
    }

    if (heap._get_block_size(oldp) >= size)
    {
        return oldp;
    }
    if(heap._check_merge(oldp,size)){
        return heap._merge_blocks_if_needed(oldp,size);
    }

    void *res = smalloc(size);
    sfree(oldp);

    if (res == nullptr)
    {
        return nullptr;
    }

    memmove(res, oldp, size);

    return res;
}

size_t _num_free_blocks() {
    return heap._get_free_blocks_num();
}

size_t _num_free_bytes() {
    return heap._get_free_blocks_bytes();
}

size_t _num_allocated_blocks() {
    return heap._get_blocks_num();
}

size_t _num_allocated_bytes() {
    return heap._get_all_bytes();
}

size_t _num_meta_data_bytes() {
    return heap._get_Metadata_size() * heap._get_blocks_num();
}

size_t _size_meta_data() {
    return heap._get_Metadata_size();
}