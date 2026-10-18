#ifndef _SHARED_PTR_H
#define _SHARED_PTR_H

#include <stddef.h>
#include <stdint.h>
#include <memory/__memory_base.h>

namespace std {

template<typename T>
class shared_ptr {
    template<typename Y> friend class shared_ptr;
    template<typename Y> friend class weak_ptr;

    T* _M_ptr;
    __shared_count _M_refcount;

public:
    using element_type = T;
    using pointer = T*;
    using reference = T&;

    constexpr shared_ptr() noexcept : _M_ptr(nullptr), _M_refcount() {}
    constexpr shared_ptr(nullptr_t) noexcept : _M_ptr(nullptr), _M_refcount() {}

    template<typename Y>
    explicit shared_ptr(Y* p) : _M_ptr(p), _M_refcount(p) {
        static_assert(sizeof(Y) > 0, "incomplete type");
    }

    template<typename Y, typename Deleter>
    shared_ptr(Y* p, Deleter d) : _M_ptr(p), _M_refcount(p, d) {
        static_assert(sizeof(Y) > 0, "incomplete type");
    }

    template<typename Y, typename Deleter, typename Alloc>
    shared_ptr(Y* p, Deleter d, Alloc a) : _M_ptr(p), _M_refcount(p, d, a) {
        static_assert(sizeof(Y) > 0, "incomplete type");
    }

    template<typename Y>
    shared_ptr(const shared_ptr<Y>& r, T* p) noexcept : _M_ptr(p), _M_refcount(r._M_refcount) {}

    shared_ptr(const shared_ptr& r) noexcept : _M_ptr(r._M_ptr), _M_refcount(r._M_refcount) {}
    template<typename Y>
    shared_ptr(const shared_ptr<Y>& r) noexcept : _M_ptr(r._M_ptr), _M_refcount(r._M_refcount) {}

    shared_ptr(shared_ptr&& r) noexcept : _M_ptr(r._M_ptr), _M_refcount(std::move(r._M_refcount)) {
        r._M_ptr = nullptr;
    }
    template<typename Y>
    shared_ptr(shared_ptr<Y>&& r) noexcept : _M_ptr(r._M_ptr), _M_refcount(std::move(r._M_refcount)) {
        r._M_ptr = nullptr;
    }

    template<typename Y>
    explicit shared_ptr(const weak_ptr<Y>& r) : _M_ptr(r._M_ptr), _M_refcount(r._M_refcount) {}

    template<typename Y, typename Deleter>
    shared_ptr(unique_ptr<Y, Deleter>&& r) : _M_ptr(r._M_ptr), _M_refcount(r._M_ptr, std::move(r._M_deleter)) {
        r._M_ptr = nullptr;
    }

    ~shared_ptr() = default;

    shared_ptr& operator=(const shared_ptr& r) noexcept {
        _M_ptr = r._M_ptr;
        _M_refcount = r._M_refcount;
        return *this;
    }
    template<typename Y>
    shared_ptr& operator=(const shared_ptr<Y>& r) noexcept {
        _M_ptr = r._M_ptr;
        _M_refcount = r._M_refcount;
        return *this;
    }

    shared_ptr& operator=(shared_ptr&& r) noexcept {
        _M_ptr = r._M_ptr;
        _M_refcount = std::move(r._M_refcount);
        r._M_ptr = nullptr;
        return *this;
    }
    template<typename Y>
    shared_ptr& operator=(shared_ptr<Y>&& r) noexcept {
        _M_ptr = r._M_ptr;
        _M_refcount = std::move(r._M_refcount);
        r._M_ptr = nullptr;
        return *this;
    }

    template<typename Y, typename Deleter>
    shared_ptr& operator=(unique_ptr<Y, Deleter>&& r) {
        shared_ptr(std::move(r)).swap(*this);
        return *this;
    }

    shared_ptr& operator=(nullptr_t) noexcept {
        shared_ptr().swap(*this);
        return *this;
    }

    void reset() noexcept { shared_ptr().swap(*this); }
    template<typename Y>
    void reset(Y* p) { shared_ptr(p).swap(*this); }
    template<typename Y, typename Deleter>
    void reset(Y* p, Deleter d) { shared_ptr(p, d).swap(*this); }
    template<typename Y, typename Deleter, typename Alloc>
    void reset(Y* p, Deleter d, Alloc a) { shared_ptr(p, d, a).swap(*this); }

    T& operator*() const noexcept { return *_M_ptr; }
    T* operator->() const noexcept { return _M_ptr; }
    T* get() const noexcept { return _M_ptr; }

    explicit operator bool() const noexcept { return _M_ptr != nullptr; }
    bool unique() const noexcept { return _M_refcount.unique(); }
    long use_count() const noexcept { return _M_refcount.use_count(); }

    void swap(shared_ptr& r) noexcept {
        std::swap(_M_ptr, r._M_ptr);
        _M_refcount.swap(r._M_refcount);
    }

    template<typename U>
    bool owner_before(const shared_ptr<U>& r) const noexcept {
        return _M_refcount._M_get_pi() < r._M_refcount._M_get_pi();
    }
    template<typename U>
    bool owner_before(const weak_ptr<U>& r) const noexcept {
        return _M_refcount._M_get_pi() < r._M_refcount._M_get_pi();
    }
};

template<typename T>
class weak_ptr {
    template<typename Y> friend class shared_ptr;
    template<typename Y> friend class weak_ptr;

    T* _M_ptr;
    __weak_count _M_refcount;

public:
    using element_type = T;
    using pointer = T*;
    using reference = T&;

    constexpr weak_ptr() noexcept : _M_ptr(nullptr), _M_refcount() {}
    constexpr weak_ptr(nullptr_t) noexcept : _M_ptr(nullptr), _M_refcount() {}

    weak_ptr(const weak_ptr& r) noexcept : _M_ptr(r._M_ptr), _M_refcount(r._M_refcount) {}
    template<typename Y>
    weak_ptr(const weak_ptr<Y>& r) noexcept : _M_ptr(r._M_ptr), _M_refcount(r._M_refcount) {}

    weak_ptr(weak_ptr&& r) noexcept : _M_ptr(r._M_ptr), _M_refcount(std::move(r._M_refcount)) {
        r._M_ptr = nullptr;
    }
    template<typename Y>
    weak_ptr(weak_ptr<Y>&& r) noexcept : _M_ptr(r._M_ptr), _M_refcount(std::move(r._M_refcount)) {
        r._M_ptr = nullptr;
    }

    template<typename Y>
    weak_ptr(const shared_ptr<Y>& r) noexcept : _M_ptr(r._M_ptr), _M_refcount(r._M_refcount) {}

    ~weak_ptr() = default;

    weak_ptr& operator=(const weak_ptr& r) noexcept {
        _M_ptr = r._M_ptr;
        _M_refcount = r._M_refcount;
        return *this;
    }
    template<typename Y>
    weak_ptr& operator=(const weak_ptr<Y>& r) noexcept {
        _M_ptr = r._M_ptr;
        _M_refcount = r._M_refcount;
        return *this;
    }

    weak_ptr& operator=(weak_ptr&& r) noexcept {
        _M_ptr = r._M_ptr;
        _M_refcount = std::move(r._M_refcount);
        r._M_ptr = nullptr;
        return *this;
    }
    template<typename Y>
    weak_ptr& operator=(weak_ptr<Y>&& r) noexcept {
        _M_ptr = r._M_ptr;
        _M_refcount = std::move(r._M_refcount);
        r._M_ptr = nullptr;
        return *this;
    }

    template<typename Y>
    weak_ptr& operator=(const shared_ptr<Y>& r) noexcept {
        _M_ptr = r._M_ptr;
        _M_refcount = r._M_refcount;
        return *this;
    }

    void reset() noexcept { weak_ptr().swap(*this); }
    void swap(weak_ptr& r) noexcept {
        std::swap(_M_ptr, r._M_ptr);
        _M_refcount.swap(r._M_refcount);
    }

    long use_count() const noexcept { return _M_refcount.use_count(); }
    bool expired() const noexcept { return _M_refcount.expired(); }

    shared_ptr<T> lock() const noexcept {
        return expired() ? shared_ptr<T>() : shared_ptr<T>(*this);
    }

    template<typename U>
    bool owner_before(const shared_ptr<U>& r) const noexcept {
        return _M_refcount._M_get_pi() < r._M_refcount._M_get_pi();
    }
    template<typename U>
    bool owner_before(const weak_ptr<U>& r) const noexcept {
        return _M_refcount._M_get_pi() < r._M_refcount._M_get_pi();
    }
};

template<typename T>
class enable_shared_from_this {
    mutable weak_ptr<T> _M_weak_this;

protected:
    constexpr enable_shared_from_this() noexcept = default;
    enable_shared_from_this(const enable_shared_from_this&) noexcept {}
    enable_shared_from_this& operator=(const enable_shared_from_this&) noexcept { return *this; }
    ~enable_shared_from_this() = default;

    shared_ptr<T> shared_from_this() {
        return shared_ptr<T>(_M_weak_this);
    }
    shared_ptr<const T> shared_from_this() const {
        return shared_ptr<const T>(_M_weak_this);
    }
    weak_ptr<T> weak_from_this() noexcept {
        return _M_weak_this;
    }
    weak_ptr<const T> weak_from_this() const noexcept {
        return _M_weak_this;
    }

    template<typename Y, typename... Args>
    friend shared_ptr<Y> make_shared(Args&&... args);
    template<typename Y, typename Alloc, typename... Args>
    friend shared_ptr<Y> allocate_shared(const Alloc& a, Args&&... args);
};

template<typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args) {
    using alloc_type = allocator<__sp_counted_ptr_inplace<T, allocator<void>>>;
    alloc_type a;
    return shared_ptr<T>(_Sp_inplace, a, std::forward<Args>(args)...);
}

template<typename T, typename Alloc, typename... Args>
shared_ptr<T> allocate_shared(const Alloc& a, Args&&... args) {
    using alloc_type = allocator<__sp_counted_ptr_inplace<T, Alloc>>;
    alloc_type alloc(a);
    return shared_ptr<T>(_Sp_inplace, alloc, std::forward<Args>(args)...);
}

template<typename T>
shared_ptr<T> make_shared_for_overwrite() {
    using alloc_type = allocator<__sp_counted_ptr_inplace<T, allocator<void>>>;
    alloc_type a;
    return shared_ptr<T>(_Sp_inplace, a);
}

template<typename T, typename Alloc>
shared_ptr<T> allocate_shared_for_overwrite(const Alloc& a) {
    using alloc_type = allocator<__sp_counted_ptr_inplace<T, Alloc>>;
    alloc_type alloc(a);
    return shared_ptr<T>(_Sp_inplace, alloc);
}

template<typename T, typename... Args>
shared_ptr<T[]> make_shared(size_t n) {
    using U = remove_extent_t<T>;
    using alloc_type = allocator<__sp_counted_ptr_inplace<U[], allocator<void>>>;
    alloc_type a;
    return shared_ptr<T>(_Sp_inplace, a, n);
}

template<typename T, typename Alloc, typename... Args>
shared_ptr<T[]> allocate_shared(const Alloc& a, size_t n) {
    using U = remove_extent_t<T>;
    using alloc_type = allocator<__sp_counted_ptr_inplace<U[], Alloc>>;
    alloc_type alloc(a);
    return shared_ptr<T>(_Sp_inplace, alloc, n);
}

template<typename T, typename U>
bool operator==(const shared_ptr<T>& x, const shared_ptr<U>& y) noexcept {
    return x.get() == y.get();
}
template<typename T, typename U>
bool operator!=(const shared_ptr<T>& x, const shared_ptr<U>& y) noexcept {
    return x.get() != y.get();
}
template<typename T, typename U>
bool operator<(const shared_ptr<T>& x, const shared_ptr<U>& y) noexcept {
    return x.get() < y.get();
}
template<typename T, typename U>
bool operator<=(const shared_ptr<T>& x, const shared_ptr<U>& y) noexcept {
    return x.get() <= y.get();
}
template<typename T, typename U>
bool operator>(const shared_ptr<T>& x, const shared_ptr<U>& y) noexcept {
    return x.get() > y.get();
}
template<typename T, typename U>
bool operator>=(const shared_ptr<T>& x, const shared_ptr<U>& y) noexcept {
    return x.get() >= y.get();
}

template<typename T>
bool operator==(const shared_ptr<T>& x, nullptr_t) noexcept {
    return !x;
}
template<typename T>
bool operator==(nullptr_t, const shared_ptr<T>& y) noexcept {
    return !y;
}
template<typename T>
bool operator!=(const shared_ptr<T>& x, nullptr_t) noexcept {
    return static_cast<bool>(x);
}
template<typename T>
bool operator!=(nullptr_t, const shared_ptr<T>& y) noexcept {
    return static_cast<bool>(y);
}
template<typename T>
bool operator<(const shared_ptr<T>& x, nullptr_t) noexcept {
    return x.get() < nullptr;
}
template<typename T>
bool operator<(nullptr_t, const shared_ptr<T>& y) noexcept {
    return nullptr < y.get();
}
template<typename T>
bool operator<=(const shared_ptr<T>& x, nullptr_t) noexcept {
    return x.get() <= nullptr;
}
template<typename T>
bool operator<=(nullptr_t, const shared_ptr<T>& y) noexcept {
    return nullptr <= y.get();
}
template<typename T>
bool operator>(const shared_ptr<T>& x, nullptr_t) noexcept {
    return x.get() > nullptr;
}
template<typename T>
bool operator>(nullptr_t, const shared_ptr<T>& y) noexcept {
    return nullptr > y.get();
}
template<typename T>
bool operator>=(const shared_ptr<T>& x, nullptr_t) noexcept {
    return x.get() >= nullptr;
}
template<typename T>
bool operator>=(nullptr_t, const shared_ptr<T>& y) noexcept {
    return nullptr >= y.get();
}

template<typename T, typename U>
bool operator==(const weak_ptr<T>& x, const shared_ptr<U>& y) noexcept {
    return x.lock().get() == y.get();
}
template<typename T, typename U>
bool operator==(const shared_ptr<T>& x, const weak_ptr<U>& y) noexcept {
    return x.get() == y.lock().get();
}
template<typename T, typename U>
bool operator==(const weak_ptr<T>& x, const weak_ptr<U>& y) noexcept {
    return x.lock().get() == y.lock().get();
}

template<typename T>
void swap(shared_ptr<T>& x, shared_ptr<T>& y) noexcept {
    x.swap(y);
}
template<typename T>
void swap(weak_ptr<T>& x, weak_ptr<T>& y) noexcept {
    x.swap(y);
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

template<typename T, typename U>
weak_ptr<T> make_weak_ptr(const shared_ptr<U>& r) noexcept {
    return weak_ptr<T>(r);
}

struct owner_less {
    template<typename T, typename U>
    bool operator()(const shared_ptr<T>& x, const shared_ptr<U>& y) const noexcept {
        return x.owner_before(y);
    }
    template<typename T, typename U>
    bool operator()(const weak_ptr<T>& x, const weak_ptr<U>& y) const noexcept {
        return x.owner_before(y);
    }
    template<typename T, typename U>
    bool operator()(const shared_ptr<T>& x, const weak_ptr<U>& y) const noexcept {
        return x.owner_before(y);
    }
    template<typename T, typename U>
    bool operator()(const weak_ptr<T>& x, const shared_ptr<U>& y) const noexcept {
        return x.owner_before(y);
    }
};

} // namespace std

#endif