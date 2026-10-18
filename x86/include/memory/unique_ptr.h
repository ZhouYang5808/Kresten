#ifndef _UNIQUE_PTR_H
#define _UNIQUE_PTR_H

#include <stddef.h>
#include <stdint.h>
#include <memory/__memory_base.h>

namespace std {

template<typename T>
struct default_delete {
    constexpr default_delete() noexcept = default;
    template<typename U>
    default_delete(const default_delete<U>&) noexcept requires is_convertible_v<U*, T*>
    {}

    void operator()(T* p) const noexcept {
        static_assert(sizeof(T) > 0, "can't delete incomplete type");
        delete p;
    }
};

template<typename T>
struct default_delete<T[]> {
    constexpr default_delete() noexcept = default;
    template<typename U>
    default_delete(const default_delete<U[]>&) noexcept requires is_convertible_v<U(*)[], T(*)[]>
    {}

    void operator()(T* p) const noexcept {
        static_assert(sizeof(T) > 0, "can't delete incomplete type");
        delete[] p;
    }
};

template<typename T, typename Deleter = default_delete<T>>
class unique_ptr {
    T* _M_ptr;
    Deleter _M_deleter;

public:
    using pointer = T*;
    using element_type = T;
    using deleter_type = Deleter;

    constexpr unique_ptr() noexcept : _M_ptr(nullptr), _M_deleter() {}
    constexpr unique_ptr(nullptr_t) noexcept : _M_ptr(nullptr), _M_deleter() {}
    explicit unique_ptr(pointer p) noexcept : _M_ptr(p), _M_deleter() {}
    unique_ptr(pointer p, deleter_type&& d) noexcept : _M_ptr(p), _M_deleter(std::move(d)) {}
    unique_ptr(pointer p, const deleter_type& d) noexcept : _M_ptr(p), _M_deleter(d) {}

    template<typename U, typename E>
    unique_ptr(unique_ptr<U, E>&& u) noexcept
        : _M_ptr(u._M_ptr), _M_deleter(std::move(u._M_deleter)) {
        u._M_ptr = nullptr;
    }

    unique_ptr(unique_ptr&& u) noexcept
        : _M_ptr(u._M_ptr), _M_deleter(std::move(u._M_deleter)) {
        u._M_ptr = nullptr;
    }

    template<typename U, typename E>
    unique_ptr& operator=(unique_ptr<U, E>&& u) noexcept {
        reset(u._M_ptr);
        _M_deleter = std::move(u._M_deleter);
        u._M_ptr = nullptr;
        return *this;
    }

    unique_ptr& operator=(unique_ptr&& u) noexcept {
        if (this != &u) {
            reset(u._M_ptr);
            _M_deleter = std::move(u._M_deleter);
            u._M_ptr = nullptr;
        }
        return *this;
    }

    unique_ptr& operator=(nullptr_t) noexcept {
        reset();
        return *this;
    }

    ~unique_ptr() {
        if (_M_ptr)
            _M_deleter(_M_ptr);
    }

    pointer get() const noexcept { return _M_ptr; }
    deleter_type& get_deleter() noexcept { return _M_deleter; }
    const deleter_type& get_deleter() const noexcept { return _M_deleter; }

    explicit operator bool() const noexcept { return _M_ptr != nullptr; }

    _Sp_reference_type<T> operator*() const noexcept { return *_M_ptr; }
    pointer operator->() const noexcept { return _M_ptr; }

    pointer release() noexcept {
        pointer p = _M_ptr;
        _M_ptr = nullptr;
        return p;
    }

    void reset(pointer p = nullptr) noexcept {
        if (_M_ptr != p) {
            if (_M_ptr)
                _M_deleter(_M_ptr);
            _M_ptr = p;
        }
    }

    void swap(unique_ptr& u) noexcept {
        std::swap(_M_ptr, u._M_ptr);
        std::swap(_M_deleter, u._M_deleter);
    }
};

template<typename T, typename Deleter>
class unique_ptr<T[], Deleter> {
    T* _M_ptr;
    Deleter _M_deleter;

public:
    using pointer = T*;
    using element_type = T;
    using deleter_type = Deleter;

    constexpr unique_ptr() noexcept : _M_ptr(nullptr), _M_deleter() {}
    constexpr unique_ptr(nullptr_t) noexcept : _M_ptr(nullptr), _M_deleter() {}
    explicit unique_ptr(pointer p) noexcept : _M_ptr(p), _M_deleter() {}
    unique_ptr(pointer p, deleter_type&& d) noexcept : _M_ptr(p), _M_deleter(std::move(d)) {}
    unique_ptr(pointer p, const deleter_type& d) noexcept : _M_ptr(p), _M_deleter(d) {}

    template<typename U, typename E>
    unique_ptr(unique_ptr<U, E>&& u) noexcept
        : _M_ptr(u._M_ptr), _M_deleter(std::move(u._M_deleter)) {
        u._M_ptr = nullptr;
    }

    unique_ptr(unique_ptr&& u) noexcept
        : _M_ptr(u._M_ptr), _M_deleter(std::move(u._M_deleter)) {
        u._M_ptr = nullptr;
    }

    template<typename U, typename E>
    unique_ptr& operator=(unique_ptr<U, E>&& u) noexcept {
        reset(u._M_ptr);
        _M_deleter = std::move(u._M_deleter);
        u._M_ptr = nullptr;
        return *this;
    }

    unique_ptr& operator=(unique_ptr&& u) noexcept {
        if (this != &u) {
            reset(u._M_ptr);
            _M_deleter = std::move(u._M_deleter);
            u._M_ptr = nullptr;
        }
        return *this;
    }

    unique_ptr& operator=(nullptr_t) noexcept {
        reset();
        return *this;
    }

    ~unique_ptr() {
        if (_M_ptr)
            _M_deleter(_M_ptr);
    }

    pointer get() const noexcept { return _M_ptr; }
    deleter_type& get_deleter() noexcept { return _M_deleter; }
    const deleter_type& get_deleter() const noexcept { return _M_deleter; }

    explicit operator bool() const noexcept { return _M_ptr != nullptr; }

    T& operator[](size_t i) const noexcept { return _M_ptr[i]; }

    pointer release() noexcept {
        pointer p = _M_ptr;
        _M_ptr = nullptr;
        return p;
    }

    void reset(pointer p = nullptr) noexcept {
        if (_M_ptr != p) {
            if (_M_ptr)
                _M_deleter(_M_ptr);
            _M_ptr = p;
        }
    }

    void swap(unique_ptr& u) noexcept {
        std::swap(_M_ptr, u._M_ptr);
        std::swap(_M_deleter, u._M_deleter);
    }
};

template<typename T, typename... Args>
unique_ptr<T> make_unique(Args&&... args) {
    return unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template<typename T>
unique_ptr<T> make_unique(size_t n) {
    using U = remove_extent_t<T>;
    return unique_ptr<T>(new U[n]());
}

template<typename T, typename... Args>
unique_ptr<T> make_unique_for_overwrite(Args&&... args) {
    return unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template<typename T>
unique_ptr<T> make_unique_for_overwrite(size_t n) {
    using U = remove_extent_t<T>;
    return unique_ptr<T>(new U[n]);
}

template<typename T, typename Deleter>
bool operator==(const unique_ptr<T, Deleter>& x, const unique_ptr<T, Deleter>& y) noexcept {
    return x.get() == y.get();
}
template<typename T, typename Deleter>
bool operator!=(const unique_ptr<T, Deleter>& x, const unique_ptr<T, Deleter>& y) noexcept {
    return x.get() != y.get();
}
template<typename T, typename Deleter>
bool operator<(const unique_ptr<T, Deleter>& x, const unique_ptr<T, Deleter>& y) noexcept {
    return x.get() < y.get();
}
template<typename T, typename Deleter>
bool operator<=(const unique_ptr<T, Deleter>& x, const unique_ptr<T, Deleter>& y) noexcept {
    return x.get() <= y.get();
}
template<typename T, typename Deleter>
bool operator>(const unique_ptr<T, Deleter>& x, const unique_ptr<T, Deleter>& y) noexcept {
    return x.get() > y.get();
}
template<typename T, typename Deleter>
bool operator>=(const unique_ptr<T, Deleter>& x, const unique_ptr<T, Deleter>& y) noexcept {
    return x.get() >= y.get();
}

template<typename T, typename Deleter>
bool operator==(const unique_ptr<T, Deleter>& x, nullptr_t) noexcept {
    return !x;
}
template<typename T, typename Deleter>
bool operator==(nullptr_t, const unique_ptr<T, Deleter>& y) noexcept {
    return !y;
}
template<typename T, typename Deleter>
bool operator!=(const unique_ptr<T, Deleter>& x, nullptr_t) noexcept {
    return static_cast<bool>(x);
}
template<typename T, typename Deleter>
bool operator!=(nullptr_t, const unique_ptr<T, Deleter>& y) noexcept {
    return static_cast<bool>(y);
}
template<typename T, typename Deleter>
bool operator<(const unique_ptr<T, Deleter>& x, nullptr_t) noexcept {
    return x.get() < nullptr;
}
template<typename T, typename Deleter>
bool operator<(nullptr_t, const unique_ptr<T, Deleter>& y) noexcept {
    return nullptr < y.get();
}
template<typename T, typename Deleter>
bool operator<=(const unique_ptr<T, Deleter>& x, nullptr_t) noexcept {
    return x.get() <= nullptr;
}
template<typename T, typename Deleter>
bool operator<=(nullptr_t, const unique_ptr<T, Deleter>& y) noexcept {
    return nullptr <= y.get();
}
template<typename T, typename Deleter>
bool operator>(const unique_ptr<T, Deleter>& x, nullptr_t) noexcept {
    return x.get() > nullptr;
}
template<typename T, typename Deleter>
bool operator>(nullptr_t, const unique_ptr<T, Deleter>& y) noexcept {
    return nullptr > y.get();
}
template<typename T, typename Deleter>
bool operator>=(const unique_ptr<T, Deleter>& x, nullptr_t) noexcept {
    return x.get() >= nullptr;
}
template<typename T, typename Deleter>
bool operator>=(nullptr_t, const unique_ptr<T, Deleter>& y) noexcept {
    return nullptr >= y.get();
}

template<typename T, typename Deleter>
void swap(unique_ptr<T, Deleter>& x, unique_ptr<T, Deleter>& y) noexcept {
    x.swap(y);
}

} // namespace std

#endif