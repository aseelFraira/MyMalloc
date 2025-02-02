#include <unistd.h>
#include <string.h>
#include <cmath>
#include <sys/mman.h>
#include <iostream>


#define MAX_MEM 100000000
#define MAX_ORDER 10
#define MAX_BLOCK_SIZE (128 * 1024)
#define NUM_BLOCKS 32
#define HUGEPAGE_THRESHOLD_SMALLOC (4 * 1024 * 1024) // 4MB
#define HUGEPAGE_THRESHOLD_SCALLOC (2 * 1024 * 1024) // 2MB



struct MallocMetadata {
    size_t m_data_size;
    size_t m_size;
    bool m_is_free;
    bool m_is_hugepage; // New field for HugePage tracking
    MallocMetadata *m_next;
    MallocMetadata *m_prev;
};

struct list {
    MallocMetadata *m_head;
    MallocMetadata *m_tail;
    size_t m_size;

    void insert(MallocMetadata *m);

    void remove(MallocMetadata *m);
};

void list::insert(MallocMetadata *m) {
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
        MallocMetadata *current = m_head;
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

void list::remove(MallocMetadata *m) {

    if (m_size == 1) {
        m_head = nullptr;
        m_tail = nullptr;
    } else if (m_tail == m) {
        m_tail = m->m_prev;
        m_tail->m_next = nullptr;
        m->m_prev = nullptr;
    } else if (m_head == m) {
        m_head = m->m_next;
        m_head->m_prev = nullptr;
        m->m_next = nullptr;
    } else {
        m->m_prev->m_next = m->m_next;
        m->m_next->m_prev = m->m_prev;
        m->m_next = nullptr;
        m->m_prev = nullptr;
    }
    m_size--;


}

class Heap {
private:
    size_t _blocks_num;
    size_t _free_blocks_num;
    size_t _free_blocks_bytes;
    // size_t _allocated_blocks_bytes;
    //  size_t _allocated_blocks_num;
    size_t _diff;
    size_t _all_bytes;
    list _allocated_blocks[MAX_ORDER + 1];
    list _free_blocks[MAX_ORDER + 1];
    bool _is_first_time;

public:
    void _init();

    Heap() : _blocks_num(0), _free_blocks_num(0), _free_blocks_bytes(0),
             _diff(0), _all_bytes(0), _is_first_time(true) {}

    size_t _get_blocks_num() const;

    //  size_t _get_allocated_blocks_num() const;
    size_t _get_allocated_blocks_bytes() const;

    size_t _get_free_blocks_num() const;

    size_t _get_free_blocks_bytes() const;

    size_t _get_Metadata_size() const;

    size_t _get_block_size(void *p) const;

    size_t _get_all_bytes() const;

    MallocMetadata *_getMetaDataPtr(void *ptr) const;

    MallocMetadata *_get_best_fit_block(int order);

    void _div_buddies(int order);

    bool _check_merge(void *oldp, size_t size);

    void _merge_blocks_if_needed(void *oldp, size_t size);


    void *_alloc_block(size_t size);

    void _free_block(void *p);

    void _merge_buddies(size_t order);


};


MallocMetadata *Heap::_getMetaDataPtr(void *ptr) const {
    if (!ptr) {
        return nullptr;
    }
    return (MallocMetadata *) ((char *) ptr - sizeof(MallocMetadata));
}

size_t Heap::_get_all_bytes() const {
    return _all_bytes;
}

void Heap::_init() {
    if (!_is_first_time) {
        return;
    }
    _is_first_time = false;

    // Get the current program break
    void *heap_break = sbrk(0);
    size_t chunk_size = MAX_BLOCK_SIZE * NUM_BLOCKS; // 32 * 128 KB = 4 MB
    intptr_t break_address = (intptr_t) (heap_break);

    // Align the program break to the next multiple of chunk_size (4 MB)
    intptr_t alignment = (break_address + chunk_size - 1) & ~(chunk_size - 1);
    size_t alignment_offset = alignment - break_address;

    // Adjust the program break to ensure alignment and allocate memory
    void *newPtr = sbrk(chunk_size + alignment_offset);
    if (!newPtr) {
        return;
    }

    // Adjust newPtr to the aligned starting address
    newPtr = (char *) newPtr + alignment_offset;

    // Initialize free and allocated block lists
    for (int i = 0; i <= MAX_ORDER; i++) {
        _free_blocks[i] = {nullptr, nullptr, 0};
        _allocated_blocks[i] = {nullptr, nullptr, 0};
    }

    // Initialize metadata for the 32 blocks and add them to the free list of MAX_ORDER
    for (int i = 0; i < NUM_BLOCKS; i++) {
        void *blockStart = (char *) newPtr + i * MAX_BLOCK_SIZE;
        MallocMetadata *newMeta = reinterpret_cast<MallocMetadata *>(blockStart);

        // Initialize metadata for each block
        newMeta->m_is_free = true;
        newMeta->m_is_hugepage = false;
        newMeta->m_data_size = MAX_BLOCK_SIZE - _get_Metadata_size(); // Usable size
        newMeta->m_size = MAX_BLOCK_SIZE;                              // Total size
        newMeta->m_next = nullptr;
        newMeta->m_prev = nullptr;

        // Insert block into the free list of MAX_ORDER
        _free_blocks[MAX_ORDER].insert(newMeta);
    }

    // Update heap statistics
    _free_blocks_num = NUM_BLOCKS;
    _free_blocks_bytes =
            MAX_BLOCK_SIZE * NUM_BLOCKS - _get_Metadata_size() * NUM_BLOCKS;
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
    for (int i = order + 1; i <= MAX_ORDER; i++) {
        if (_free_blocks[i].m_size > 0) {
            free_order = i;
            break;
        }
    }
    if (free_order == -1) {
        return;
    }
    for (int j = free_order; j > order; j--) {
        MallocMetadata *temp = _free_blocks[j].m_head;
        MallocMetadata *buddy1, *buddy2;

        buddy1 = temp;
        size_t blockSize = (temp->m_data_size - _get_Metadata_size());


        buddy1->m_data_size = blockSize / 2;
        buddy1->m_is_free = true;
        buddy1->m_size = (temp->m_size) / 2;

        buddy2 = (MallocMetadata *) ((char *) temp + (buddy1->m_size));
        buddy2->m_is_free = true;
        buddy2->m_data_size = buddy1->m_data_size;
        buddy2->m_size = buddy1->m_size;

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
        return nullptr; // Null check for safety
    }

    _free_blocks[order].remove(res);
    _allocated_blocks[order].insert(res);
    _free_blocks_bytes -= res->m_data_size;

    res->m_is_free = false;
    return res;
}

void* Heap::_alloc_block(size_t size) {
    if (size >= HUGEPAGE_THRESHOLD_SMALLOC) {
        size_t huge_page_size = 2 * 1024 * 1024; // 2MB HugePage size
        size_t aligned_size = ((size + huge_page_size - 1) / huge_page_size) * huge_page_size;

        std::cerr << "Attempting HugePage allocation. Size: " << aligned_size << std::endl;

        void* ptr = mmap(nullptr, aligned_size + sizeof(MallocMetadata),
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);

        if (ptr == MAP_FAILED) {
            perror("HugePage mmap failed. Falling back to normal mmap.");
            ptr = mmap(nullptr, aligned_size + sizeof(MallocMetadata),
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

            if (ptr == MAP_FAILED) {
                perror("Fallback mmap also failed");
                return nullptr;
            }
        }

        MallocMetadata* newBlock = (MallocMetadata*)ptr;
        newBlock->m_data_size = aligned_size;
        newBlock->m_size = aligned_size + sizeof(MallocMetadata);
        newBlock->m_is_free = false;
        newBlock->m_is_hugepage = true;

        std::cerr << "HugePage allocation succeeded. Address: " << ptr << std::endl;
        return (char*)ptr + sizeof(MallocMetadata);
    }

    // Default allocation logic for smaller sizes
    size_t needed_size = static_cast<size_t>(ceil((size + sizeof(MallocMetadata)) / 128.0));
    int order = std::ceil(std::log2(std::ceil(needed_size)));

    if (order > MAX_ORDER) {
        return nullptr; // Cannot allocate beyond MAX_ORDER size
    }

    MallocMetadata* ptr = _get_best_fit_block(order);
    if (ptr) {
        _free_blocks_num--;
        return (char*)ptr + sizeof(MallocMetadata); // Return usable block
    }
    return nullptr;
}


void Heap::_free_block(void *p) {
    MallocMetadata *temp = _getMetaDataPtr(p);
    if (!temp) {
        return;
    }

    // Check if it's a HugePage allocation
    if (temp->m_size > HUGEPAGE_THRESHOLD_SMALLOC) {
        munmap(temp, temp->m_size); // Free HugePage memory
        _blocks_num--;
        _all_bytes -= temp->m_data_size;
        return;
    }

    // Regular deallocation logic for non-HugePage blocks
    if (temp->m_is_free) {
        return;
    }
    temp->m_is_free = true;
    size_t order = std::ceil(
            std::log2(ceil(temp->m_size / static_cast<double>(128))));
    _allocated_blocks[order].remove(temp);
    _free_blocks[order].insert(temp);
    _free_blocks_num++;
    _free_blocks_bytes += temp->m_data_size;

    _merge_buddies(order); // Attempt to merge buddies
}

void Heap::_merge_buddies(size_t order) {
    if (order == MAX_ORDER) {
        return;
    }
    MallocMetadata *currBlock = _free_blocks[order].m_head;
    if (!currBlock) { //i think its rudnadnt
        return;
    }
    while (currBlock && currBlock->m_next) {
        intptr_t buddy_address =
                reinterpret_cast<intptr_t>(currBlock) ^ currBlock->m_size;
        if (buddy_address == reinterpret_cast<intptr_t>(currBlock->m_next)) {

            MallocMetadata *nextBlock = currBlock->m_next;
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

size_t Heap::_get_block_size(void *p) const {
    MallocMetadata *curr = _getMetaDataPtr(p);
    if (curr) {
        return curr->m_data_size;
    }
    return -1;
}

bool Heap::_check_merge(void *oldp, size_t size) {
    MallocMetadata *p = _getMetaDataPtr(oldp);
    size_t curr_size = p->m_size;

    while (true) {
        // Compute the next block by moving `curr_size + metadata_size` forward
        MallocMetadata *next_block = reinterpret_cast<MallocMetadata *>(
                reinterpret_cast<size_t>(p) ^ curr_size);

        // Ensure the next block is valid and free before merging
        if (!next_block || !next_block->m_is_free ||
            curr_size >= (size + _get_Metadata_size())) {
            break;
        }

        // Merge by increasing the current size
        curr_size += next_block->m_size;

        // Move pointer to the merged block
        p = next_block;
    }

    return curr_size >= (size + _get_Metadata_size());
}

void Heap::_merge_blocks_if_needed(void *oldp, size_t size) {
    MallocMetadata *p = _getMetaDataPtr(oldp);
    size_t curr_size = p->m_data_size;

    while (true) {
        // Compute the next block location
        MallocMetadata *next_block = (MallocMetadata *) (
                reinterpret_cast<intptr_t>(p) xor curr_size);

        // Stop merging if:
        // 1. The next block is not valid.
        // 2. The next block is not free.
        // 3. The total merged size already reaches/exceeds the required `size`.
        if (!next_block || !next_block->m_is_free ||
            curr_size >= (size + _get_Metadata_size())) {
            break;
        }

        // Merge the block by adding its size
        curr_size += next_block->m_size;

        _blocks_num--;
        _free_blocks_num--;
        _all_bytes += _get_Metadata_size();
        _free_blocks_bytes -= next_block->m_data_size;


        // Mark the next block as merged (if you maintain a linked list, update the links)
        p->m_size = curr_size +
                    _get_Metadata_size();  // Upda te size of merged block
        p->m_data_size = curr_size;
        next_block->m_is_free = false;  // Mark the merged block as used (prevent reuse)

        // Move to the next block for potential further merging
        p = next_block;
    }
}


Heap heap;

void *smalloc(size_t size) {
    heap._init();
    if (size <= 0 || size > MAX_MEM) {
        return nullptr;
    }
    void *ptr = heap._alloc_block(size);

    return (!ptr) ? nullptr : (char *) ptr + heap._get_Metadata_size();

}

void *scalloc(size_t num, size_t size) {
    size_t total_size = num * size;

    // Check if HugePage is required for the total allocation
    if (total_size >= HUGEPAGE_THRESHOLD_SMALLOC ||
        size >= HUGEPAGE_THRESHOLD_SCALLOC) {
        void *res = heap._alloc_block(
                total_size); // Allocate using HugePages if needed
        if (!res) {
            return nullptr;
        }
        memset(res, 0, total_size); // Initialize memory to zero
        return res;
    }

    // Use the standard `smalloc` logic for smaller blocks
    void *res = smalloc(total_size);
    if (res) {
        memset(res, 0, total_size); // Initialize memory to zero
    }
    return res;
}


void sfree(void *ptr) {
    if (ptr == nullptr) {
        return;
    }
    heap._free_block(ptr);
}

void *srealloc(void *oldp, size_t size) {
    if (size <= 0 || size > MAX_BLOCK_SIZE) {
        return nullptr;
    }

    if (oldp == nullptr) {
        return smalloc(size);
    }

    if (heap._get_block_size(oldp) >= size) {
        return oldp;
    }
    if (heap._check_merge(oldp, size)) {
        heap._merge_blocks_if_needed(oldp, size);
        return oldp;
    }
    sfree(oldp);

    void *res = smalloc(size);

    if (res == nullptr) {
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