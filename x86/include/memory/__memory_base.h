#ifndef _MEMORY_BASE_H
#define _MEMORY_BASE_H

#include <stddef.h>
#include <stdint.h>

namespace std {

struct __sp_counted_base {
    uint32_t _M_refcount;
    uint32_t _M_weakcount;

    __sp_counted_base() : _M_refcount(1), _M_weakcount(1) {}
    virtual ~__sp_counted_base() {}

    virtual void _M_dispose() = 0;
    virtual void _M_destroy() = 0;

    void _M_add_ref_copy() { __atomic_add_fetch(&_M_refcount, 1, __ATOMIC_RELAXED); }
    /* 成功 +1 返回 true; 控制块已过期 (refcount==0) 返回 false, 不修改计数 */
    bool _M_add_ref_lock() {
        uint32_t old = _M_refcount;
        while (old != 0) {
            if (__atomic_compare_exchange_n(&_M_refcount, &old, old + 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                return true;
        }
        return false;
    }
    void _M_release() {
        if (__atomic_sub_fetch(&_M_refcount, 1, __ATOMIC_RELEASE) == 0) {
            _M_dispose();
            if (__atomic_sub_fetch(&_M_weakcount, 1, __ATOMIC_RELEASE) == 0)
                _M_destroy();
        }
    }
    void _M_weak_add_ref() { __atomic_add_fetch(&_M_weakcount, 1, __ATOMIC_RELAXED); }
    void _M_weak_release() {
        if (__atomic_sub_fetch(&_M_weakcount, 1, __ATOMIC_RELEASE) == 0)
            _M_destroy();
    }
    long _M_get_use_count() const { return _M_refcount; }
};

template<typename T>
struct __sp_counted_ptr : __sp_counted_base {
    T* _M_ptr;

    __sp_counted_ptr(T* p) : _M_ptr(p) {}
    void _M_dispose() override { delete _M_ptr; }
    void _M_destroy() override { delete this; }
};

template<typename T, typename Deleter>
struct __sp_counted_deleter : __sp_counted_base {
    T* _M_ptr;
    Deleter _M_deleter;

    __sp_counted_deleter(T* p, Deleter d) : _M_ptr(p), _M_deleter(d) {}
    void _M_dispose() override { _M_deleter(_M_ptr); }
    void _M_destroy() override { delete this; }
};

template<typename T, typename Alloc>
struct __sp_counted_ptr_inplace : __sp_counted_base {
    T _M_ptr;
    Alloc _M_alloc;

    template<typename... Args>
    __sp_counted_ptr_inplace(Alloc a, Args&&... args) : _M_ptr(std::forward<Args>(args)...), _M_alloc(a) {}
    void _M_dispose() override { _M_ptr.~T(); }
    void _M_destroy() override {
        using alloc_traits = allocator_traits<Alloc>;
        alloc_traits::deallocate(_M_alloc, reinterpret_cast<typename alloc_traits::pointer>(this), 1);
    }
};

struct __weak_count {
    __sp_counted_base* _M_pi = nullptr;

    __weak_count() = default;
    __weak_count(const __weak_count& r) : _M_pi(r._M_pi) { if (_M_pi) _M_pi->_M_weak_add_ref(); }
    __weak_count(__weak_count&& r) noexcept : _M_pi(r._M_pi) { r._M_pi = nullptr; }
    ~__weak_count() { if (_M_pi) _M_pi->_M_weak_release(); }

    __weak_count& operator=(const __weak_count& r) {
        if (_M_pi != r._M_pi) {
            if (_M_pi) _M_pi->_M_weak_release();
            _M_pi = r._M_pi;
            if (_M_pi) _M_pi->_M_weak_add_ref();
        }
        return *this;
    }
    __weak_count& operator=(__weak_count&& r) noexcept {
        if (_M_pi) _M_pi->_M_weak_release();
        _M_pi = r._M_pi;
        r._M_pi = nullptr;
        return *this;
    }

    void swap(__weak_count& r) noexcept { __sp_counted_base* tmp = _M_pi; _M_pi = r._M_pi; r._M_pi = tmp; }
    long use_count() const noexcept { return _M_pi ? _M_pi->_M_get_use_count() : 0; }
    bool expired() const noexcept { return use_count() == 0; }
};

struct __shared_count {
    __sp_counted_base* _M_pi = nullptr;

    __shared_count() = default;
    template<typename T>
    explicit __shared_count(T* p) : _M_pi(new __sp_counted_ptr<T>(p)) {}
    template<typename T, typename Deleter>
    __shared_count(T* p, Deleter d) : _M_pi(new __sp_counted_deleter<T, Deleter>(p, d)) {}
    template<typename T, typename Alloc, typename... Args>
    __shared_count(std::_Sp_inplace_tag, Alloc a, Args&&... args) : _M_pi(new __sp_counted_ptr_inplace<T, Alloc>(a, std::forward<Args>(args)...)) {}

    __shared_count(const __shared_count& r) noexcept : _M_pi(r._M_pi) { if (_M_pi) _M_pi->_M_add_ref_copy(); }
    __shared_count(__shared_count&& r) noexcept : _M_pi(r._M_pi) { r._M_pi = nullptr; }
    __shared_count(const __weak_count& r) : _M_pi(r._M_pi) {
        if (_M_pi && !_M_pi->_M_add_ref_lock()) {
            /* 控制块已过期: 不能接管 (原实现设 _M_pi 后检查空, 永不触发,
             * 计数从 0 再减会下溢 0xFFFFFFFF, 控制块/对象永不释放) */
            _M_pi = nullptr;
            __throw_bad_weak_ptr();
        }
    }
    ~__shared_count() { if (_M_pi) _M_pi->_M_release(); }

    __shared_count& operator=(const __shared_count& r) noexcept {
        if (_M_pi != r._M_pi) {
            if (_M_pi) _M_pi->_M_release();
            _M_pi = r._M_pi;
            if (_M_pi) _M_pi->_M_add_ref_copy();
        }
        return *this;
    }
    __shared_count& operator=(__shared_count&& r) noexcept {
        if (_M_pi) _M_pi->_M_release();
        _M_pi = r._M_pi;
        r._M_pi = nullptr;
        return *this;
    }

    void swap(__shared_count& r) noexcept { __sp_counted_base* tmp = _M_pi; _M_pi = r._M_pi; r._M_pi = tmp; }
    long use_count() const noexcept { return _M_pi ? _M_pi->_M_get_use_count() : 0; }
    bool unique() const noexcept { return use_count() == 1; }
    bool empty() const noexcept { return _M_pi == nullptr; }
    __sp_counted_base* _M_get_pi() const noexcept { return _M_pi; }
};

template<typename T>
struct __shared_ptr_access {
    using element_type = T;
    using pointer = T*;
    using reference = T&;
};

template<typename T>
struct __shared_ptr_access<T[]> {
    using element_type = T;
    using pointer = T*;
    using reference = T&;
};

template<typename T, size_t N>
struct __shared_ptr_access<T[N]> {
    using element_type = T;
    using pointer = T*;
    using reference = T&;
};

template<typename T>
class __weak_ptr;

template<typename T>
class __shared_ptr;

template<typename T>
class __unique_ptr;

struct _Sp_inplace_tag { explicit _Sp_inplace_tag() = default; };
inline constexpr _Sp_inplace_tag _Sp_inplace{};

template<typename T>
using _Sp_element_type = typename __shared_ptr_access<T>::element_type;

template<typename T>
using _Sp_pointer_type = typename __shared_ptr_access<T>::pointer;

template<typename T>
using _Sp_reference_type = typename __shared_ptr_access<T>::reference;

void __throw_bad_weak_ptr();

} // namespace std

#endif