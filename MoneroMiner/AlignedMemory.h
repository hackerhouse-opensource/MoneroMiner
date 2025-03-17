#pragma once

#include <cstdlib>
#include <cstring>
#include <stdexcept>

// Aligned memory allocation class
class AlignedMemory {
private:
    void* ptr;
    size_t size;

public:
    explicit AlignedMemory(size_t _size, size_t alignment) : ptr(nullptr), size(_size) {
        ptr = _aligned_malloc(size, alignment);
        if (!ptr) {
            throw std::runtime_error("Failed to allocate aligned memory");
        }
        std::memset(ptr, 0, size);
    }

    ~AlignedMemory() {
        if (ptr) {
            _aligned_free(ptr);
            ptr = nullptr;
        }
    }

    void* get() const { return ptr; }
    size_t getSize() const { return size; }

    AlignedMemory(const AlignedMemory&) = delete;
    AlignedMemory& operator=(const AlignedMemory&) = delete;
    AlignedMemory(AlignedMemory&&) = delete;
    AlignedMemory& operator=(AlignedMemory&&) = delete;
}; 