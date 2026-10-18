#include <stddef.h>

namespace std {
    using ::size_t;
    using ::ptrdiff_t;
    using ::max_align_t;
    using ::nullptr_t;
    
    enum class align_val_t : size_t {};
}

extern "C" {
    void *kmalloc(size_t size);
    void kfree(void *ptr);
}

extern "C" void __cxa_pure_virtual() {
    while (1) {}
}

void *operator new(std::size_t size) {
    void *ptr = kmalloc(size);
    if (!ptr) {
        while (1) {}
    }
    return ptr;
}

void *operator new[](std::size_t size) {
    void *ptr = kmalloc(size);
    if (!ptr) {
        while (1) {}
    }
    return ptr;
}

void operator delete(void *ptr) noexcept {
    if (ptr) kfree(ptr);
}

void operator delete[](void *ptr) noexcept {
    if (ptr) kfree(ptr);
}

void *operator new(std::size_t size, std::align_val_t align) {
    void *ptr = kmalloc(size);
    if (!ptr) {
        while (1) {}
    }
    return ptr;
}

void *operator new[](std::size_t size, std::align_val_t align) {
    void *ptr = kmalloc(size);
    if (!ptr) {
        while (1) {}
    }
    return ptr;
}

void operator delete(void *ptr, std::align_val_t align) noexcept {
    if (ptr) kfree(ptr);
}

void operator delete[](void *ptr, std::align_val_t align) noexcept {
    if (ptr) kfree(ptr);
}

void operator delete(void *ptr, std::size_t size) noexcept {
    (void)size;
    if (ptr) kfree(ptr);
}

void operator delete[](void *ptr, std::size_t size) noexcept {
    (void)size;
    if (ptr) kfree(ptr);
}

namespace std {
    void __throw_bad_alloc() {
        while (1) {}
    }
    
    void __throw_bad_array_new_length() {
        while (1) {}
    }
    
    void __throw_length_error(const char*) {
        while (1) {}
    }
    
    void __throw_out_of_range(const char*) {
        while (1) {}
    }
    
    void __throw_bad_weak_ptr() {
        while (1) {}
    }
}

extern "C" int __cxa_guard_acquire(int *guard) {
    return !*(char*)guard;
}

extern "C" void __cxa_guard_release(int *guard) {
    *(char*)guard = 1;
}

extern "C" void __cxa_guard_abort(int *guard) {
    (void)guard;
}

extern "C" void __cxa_atexit(void (*destructor)(void*), void *arg, void *dso_handle) {
    (void)destructor;
    (void)arg;
    (void)dso_handle;
}

extern "C" void __cxa_finalize(void *dso_handle) {
    (void)dso_handle;
}