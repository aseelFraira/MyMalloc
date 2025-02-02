#include <unistd.h>
#include <string.h>

#define MAX_MEM 100000000

struct MallocMetadata {
    size_t m_size; // Only the block size for the user "Withoud metadata"
    bool m_is_free;
    MallocMetadata* m_next;
    MallocMetadata* m_prev;
};

class Heap {
private:
    size_t _blocks_num;
    size_t _free_blocks_num;
    size_t _free_blocks_bytes;
    size_t _all_bytes;
    MallocMetadata* _heap_list_head;
    MallocMetadata* _heap_list_tail;

public:
    Heap() : _blocks_num(0), _free_blocks_num(0), _free_blocks_bytes(0),
             _all_bytes(0),
             _heap_list_head(nullptr), _heap_list_tail(nullptr) {}

    size_t _get_blocks_num() const;
    size_t _get_free_blocks_num() const;
    size_t _get_free_blocks_bytes() const;
    size_t _get_Metadata_size() const;
    size_t _get_block_size(void* p) const;
    size_t _get_all_bytes() const;
    MallocMetadata* _get_MetaDataPtr(void* p) const;

    void _insert_block(MallocMetadata* m);
    void* _alloc_block(size_t size);
    void _free_block(void* p);
};

size_t Heap::_get_blocks_num() const {
    return _blocks_num;
}



size_t Heap::_get_free_blocks_num() const {
    return _free_blocks_num;
}

size_t Heap::_get_all_bytes() const {
    return _all_bytes;
}


size_t Heap::_get_free_blocks_bytes() const {
    return _free_blocks_bytes;
}

size_t Heap::_get_block_size(void* p) const {
    MallocMetadata* meta = _get_MetaDataPtr(p);
    return meta->m_size;
}

size_t Heap::_get_Metadata_size() const {
    return sizeof(MallocMetadata);
}

MallocMetadata* Heap::_get_MetaDataPtr(void* p) const {
    return (MallocMetadata*)((char*)p - sizeof(MallocMetadata));
}

void Heap::_insert_block(MallocMetadata* m) {
    if (_blocks_num == 0) {
        _heap_list_head = _heap_list_tail = m;
        return;
    }

    if ((size_t)m <= (size_t)_heap_list_head) { // i dont think this can happen but who knows!
        m->m_next = _heap_list_head;
        _heap_list_head->m_prev = m;
        _heap_list_head = m;
    } else if ((size_t)m >= (size_t)_heap_list_tail) { //This is what really happens!
        _heap_list_tail->m_next = m;
        m->m_prev = _heap_list_tail;
        _heap_list_tail = m;
    } else {
        MallocMetadata* current = _heap_list_head;
        while (current && current->m_next) { // i dont think this can happen but who knows!
            if ((size_t)m > (size_t)current && (size_t)m < (size_t)current->m_next) {
                m->m_next = current->m_next;
                m->m_next->m_prev = m;
                m->m_prev = current;
                current->m_next = m;
                break;
            }
            current = current->m_next;
        }
    }
}

void* Heap::_alloc_block(size_t size) {
    MallocMetadata* current = _heap_list_head;

    while (current) {
        if (current->m_is_free && current->m_size >= size) {
            current->m_is_free = false;
            _free_blocks_num--;
            _free_blocks_bytes -= current->m_size;
            return (char*)current + sizeof(MallocMetadata);
        }
        current = current->m_next;
    }

    void* ptr = sbrk(size + sizeof(MallocMetadata));
    if (ptr == (void*)-1) { // Corrected sbrk() check
        return nullptr;
    }

    MallocMetadata* new_block = (MallocMetadata*)ptr;
    new_block->m_size = size;
    new_block->m_is_free = false;
    new_block->m_next = nullptr;
    new_block->m_prev = nullptr;

    _insert_block(new_block);
    _blocks_num++;
    _all_bytes += size;
    return (char*)new_block + sizeof(MallocMetadata);
}

void Heap::_free_block(void* p) {

    MallocMetadata* block = _get_MetaDataPtr(p);
    if (block->m_is_free) {
        return;
    }
    block->m_is_free = true;
    _free_blocks_num++;
    _free_blocks_bytes += block->m_size;

}

Heap heap;

void* smalloc(size_t size) {
    if (size == 0 || size > MAX_MEM) {
        return nullptr;
    }
    return heap._alloc_block(size);
}

void* scalloc(size_t num, size_t size) {
    void* ptr = smalloc(num * size);
    if (!ptr) {
        return nullptr;
    }
    memset(ptr, 0, num * size);
    return ptr;
}

void sfree(void* p) {
    if (!p) {
        return;
    }
    heap._free_block(p);
}

void* srealloc(void* oldp, size_t size) {
    if (size == 0 || size > MAX_MEM) {
        return nullptr;
    }
    if (!oldp) {
        return smalloc(size);
    }

    size_t old_size = heap._get_block_size(oldp);
    if (old_size >= size) {
        return oldp;
    }

    void* ptr = smalloc(size);
    if (!ptr) {
        return nullptr;
    }

    memmove(ptr, oldp, old_size);
    sfree(oldp);
    return ptr;
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
    return (heap._get_all_bytes());
}

size_t _num_meta_data_bytes() {
    return heap._get_Metadata_size() * heap._get_blocks_num();
}

size_t _size_meta_data() {
    return heap._get_Metadata_size();
}
