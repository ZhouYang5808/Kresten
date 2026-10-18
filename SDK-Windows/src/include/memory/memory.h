#ifndef _MEMORY_H
#define _MEMORY_H

#include <memory/__memory_base.h>
#include <memory/unique_ptr.h>
#include <memory/shared_ptr.h>
#include <memory/weak_ptr.h>
#include <memory/make_unique.h>
#include <memory/make_shared.h>

namespace std {

template<typename T>
struct allocator {
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    template<typename U>
    struct rebind { using other = allocator<U>; };

    constexpr allocator() noexcept = default;
    constexpr allocator(const allocator&) noexcept = default;
    template<typename U>
    constexpr allocator(const allocator<U>&) noexcept {}

    T* allocate(size_type n) {
        if (n > max_size())
            __throw_bad_alloc();
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    void deallocate(T* p, size_type) noexcept {
        ::operator delete(p);
    }

    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new((void*)p) U(std::forward<Args>(args)...);
    }

    template<typename U>
    void destroy(U* p) {
        p->~U();
    }

    size_type max_size() const noexcept {
        return static_cast<size_type>(-1) / sizeof(T);
    }

    allocator select_on_container_copy_construction() const noexcept {
        return *this;
    }
};

template<typename T, typename U>
bool operator==(const allocator<T>&, const allocator<U>&) noexcept { return true; }
template<typename T, typename U>
bool operator!=(const allocator<T>&, const allocator<U>&) noexcept { return false; }

template<typename T>
struct allocator_traits {
    using allocator_type = T;
    using value_type = typename T::value_type;
    using pointer = typename T::pointer;
    using const_pointer = typename T::const_pointer;
    using void_pointer = void*;
    using const_void_pointer = const void*;
    using difference_type = typename T::difference_type;
    using size_type = typename T::size_type;
    using propagate_on_container_copy_assignment = false_type;
    using propagate_on_container_move_assignment = false_type;
    using propagate_on_container_swap = false_type;
    using is_always_equal = true_type;

    template<typename U>
    using rebind_alloc = typename T::template rebind<U>::other;

    template<typename U>
    using rebind_traits = allocator_traits<rebind_alloc<U>>;

    static pointer allocate(allocator_type& a, size_type n) {
        return a.allocate(n);
    }
    static pointer allocate(allocator_type& a, size_type n, const_void_pointer) {
        return a.allocate(n);
    }
    static void deallocate(allocator_type& a, pointer p, size_type n) {
        a.deallocate(p, n);
    }

    template<typename U, typename... Args>
    static void construct(allocator_type& a, U* p, Args&&... args) {
        a.construct(p, std::forward<Args>(args)...);
    }
    template<typename U>
    static void destroy(allocator_type& a, U* p) {
        a.destroy(p);
    }

    static size_type max_size(const allocator_type& a) noexcept {
        return a.max_size();
    }

    static allocator_type select_on_container_copy_construction(const allocator_type& a) {
        return a.select_on_container_copy_construction();
    }
};

template<typename T, typename... Args>
T* construct_at(T* p, Args&&... args) {
    return ::new((void*)p) T(std::forward<Args>(args)...);
}

template<typename T>
void destroy_at(T* p) {
    p->~T();
}

template<typename ForwardIt>
void destroy(ForwardIt first, ForwardIt last) {
    for (; first != last; ++first)
        destroy_at(std::addressof(*first));
}

template<typename ForwardIt, typename Size>
ForwardIt destroy_n(ForwardIt first, Size n) {
    for (; n > 0; ++first, --n)
        destroy_at(std::addressof(*first));
    return first;
}

template<typename T>
constexpr T* addressof(T& r) noexcept {
    return reinterpret_cast<T*>(&const_cast<char&>(reinterpret_cast<const volatile char&>(r)));
}

template<typename T>
constexpr const T* addressof(const T& r) noexcept {
    return reinterpret_cast<const T*>(&const_cast<char&>(reinterpret_cast<const volatile char&>(r)));
}

template<typename T>
constexpr T* addressof(T&&) = delete;

template<typename T>
struct pointer_traits {
    using pointer = T;
    using element_type = typename T::element_type;
    using difference_type = typename T::difference_type;

    template<typename U>
    using rebind = typename T::template rebind<U>;
};

template<typename T>
struct pointer_traits<T*> {
    using pointer = T*;
    using element_type = T;
    using difference_type = ptrdiff_t;

    template<typename U>
    using rebind = U*;
};

template<typename T>
struct pointer_traits<const T*> {
    using pointer = const T*;
    using element_type = T;
    using difference_type = ptrdiff_t;

    template<typename U>
    using rebind = U*;
};

template<typename T, typename... Args>
T* make_unique(Args&&... args) {
    return unique_ptr<T>(new T(std::forward<Args>(args)...)).release();
}

template<typename T>
T* make_unique(size_t n) {
    using U = remove_extent_t<T>;
    return unique_ptr<T>(new U[n]()).release();
}

template<typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args) {
    return shared_ptr<T>(new T(std::forward<Args>(args)...));
}

template<typename T, typename Alloc, typename... Args>
shared_ptr<T> allocate_shared(const Alloc& a, Args&&... args) {
    using alloc_type = allocator<__sp_counted_ptr_inplace<T, Alloc>>;
    alloc_type alloc(a);
    return shared_ptr<T>(_Sp_inplace, alloc, std::forward<Args>(args)...);
}

template<typename T>
shared_ptr<T> make_shared_for_overwrite() {
    return make_shared<T>();
}

template<typename T, typename Alloc>
shared_ptr<T> allocate_shared_for_overwrite(const Alloc& a) {
    return allocate_shared<T>(a);
}

template<typename T>
shared_ptr<T[]> make_shared(size_t n) {
    using U = remove_extent_t<T>;
    return shared_ptr<T>(new U[n]());
}

template<typename T, typename Alloc>
shared_ptr<T[]> allocate_shared(const Alloc& a, size_t n) {
    using U = remove_extent_t<T>;
    using alloc_type = allocator<__sp_counted_ptr_inplace<U[], Alloc>>;
    alloc_type alloc(a);
    return shared_ptr<T>(_Sp_inplace, alloc, n);
}

template<typename T, typename U>
shared_ptr<T> static_pointer_cast(const shared_ptr<U>& r) noexcept {
    return shared_ptr<T>(r, static_cast<typename shared_ptr<T>::element_type*>(r.get()));
}
template<typename T, typename U>
shared_ptr<T> dynamic_pointer_cast(const shared_ptr<U>& r) noexcept {
    auto p = dynamic_cast<typename shared_ptr<T>::element_type*>(r.get());
    return p ? shared_ptr<T>(r, p) : shared_ptr<T>();
}
template<typename T, typename U>
shared_ptr<T> const_pointer_cast(const shared_ptr<U>& r) noexcept {
    return shared_ptr<T>(r, const_cast<typename shared_ptr<T>::element_type*>(r.get()));
}
template<typename T, typename U>
shared_ptr<T> reinterpret_pointer_cast(const shared_ptr<U>& r) noexcept {
    return shared_ptr<T>(r, reinterpret_cast<typename shared_ptr<T>::element_type*>(r.get()));
}

template<typename T, typename U>
weak_ptr<T> static_pointer_cast(const weak_ptr<U>& r) noexcept {
    return weak_ptr<T>(static_pointer_cast<T>(r.lock()));
}
template<typename T, typename U>
weak_ptr<T> dynamic_pointer_cast(const weak_ptr<U>& r) noexcept {
    return weak_ptr<T>(dynamic_pointer_cast<T>(r.lock()));
}
template<typename T, typename U>
weak_ptr<T> const_pointer_cast(const weak_ptr<U>& r) noexcept {
    return weak_ptr<T>(const_pointer_cast<T>(r.lock()));
}
template<typename T, typename U>
weak_ptr<T> reinterpret_pointer_cast(const weak_ptr<U>& r) noexcept {
    return weak_ptr<T>(reinterpret_pointer_cast<T>(r.lock()));
}

template<typename T>
class auto_ptr {
    T* _M_ptr;

public:
    using element_type = T;

    explicit auto_ptr(T* p = nullptr) noexcept : _M_ptr(p) {}
    auto_ptr(auto_ptr&& p) noexcept : _M_ptr(p.release()) {}
    template<typename U>
    auto_ptr(auto_ptr<U>&& p) noexcept : _M_ptr(p.release()) {}

    auto_ptr& operator=(auto_ptr&& p) noexcept {
        reset(p.release());
        return *this;
    }
    template<typename U>
    auto_ptr& operator=(auto_ptr<U>&& p) noexcept {
        reset(p.release());
        return *this;
    }

    ~auto_ptr() { delete _M_ptr; }

    T& operator*() const noexcept { return *_M_ptr; }
    T* operator->() const noexcept { return _M_ptr; }
    T* get() const noexcept { return _M_ptr; }
    T* release() noexcept { T* p = _M_ptr; _M_ptr = nullptr; return p; }
    void reset(T* p = nullptr) noexcept {
        if (_M_ptr != p) {
            delete _M_ptr;
            _M_ptr = p;
        }
    }
    auto_ptr(auto_ptr& p) noexcept : _M_ptr(p.release()) {}
    template<typename U>
    auto_ptr(auto_ptr<U>& p) noexcept : _M_ptr(p.release()) {}
    auto_ptr& operator=(auto_ptr& p) noexcept {
        reset(p.release());
        return *this;
    }
    template<typename U>
    auto_ptr& operator=(auto_ptr<U>& p) noexcept {
        reset(p.release());
        return *this;
    }
};

} // namespace std

#endif