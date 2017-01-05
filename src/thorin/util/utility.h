#ifndef THORIN_UTILITY_H
#define THORIN_UTILITY_H

#include <cassert>
#include <memory>
#include <queue>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#ifndef _MSC_VER
#define THORIN_UNREACHABLE do { assert(true && "unreachable"); abort(); } while(0)
#else // _MSC_VER
inline __declspec(noreturn) void thorin_dummy_function() { abort(); }
#define THORIN_UNREACHABLE do { assert(true && "unreachable"); thorin_dummy_function(); } while(0)
#endif

#ifndef NDEBUG
#define THORIN_CALL_ONCE do { static bool once = true; assert(once); once=false; } while(0)
#define assert_unused(x) assert(x)
#else
#define THORIN_CALL_ONCE
#define assert_unused(x) ((void) (0 && (x)))
#endif

#define THORIN_IMPLIES(a, b) (!(a) || ((a) && (b)))

// http://stackoverflow.com/questions/1489932/how-to-concatenate-twice-with-the-c-preprocessor-and-expand-a-macro-as-in-arg
#define THORIN_PASTER(x,y) x ## y

namespace thorin {

/// Use to initialize an \p AutoPtr in a lazy way.
template<class This, class T>
inline T& lazy_init(const This* self, std::unique_ptr<T>& ptr) {
    return *(ptr ? ptr : (ptr.reset(new T(*self)), ptr));
}

template<class T>
T pop(std::queue<T>& queue) {
    auto val = queue.front();
    queue.pop();
    return val;
}

template<class T>
struct Push {
    Push(T& t, T new_val)
        : old_(t)
        , ref_(t)
    {
        t = new_val;
    }
    ~Push() { ref_ = old_; }

private:
    T old_;
    T& ref_;
};

template<class T, class U>
inline Push<T> push(T& t, U new_val) { return Push<T>(t, new_val); }

#define THORIN_LNAME__(name, line) name##__##line
#define THORIN_LNAME_(name, line)  THORIN_LNAME__(name, line)
#define THORIN_LNAME(name)         THORIN_LNAME_(name, __LINE__)

#define THORIN_PUSH(what, with) auto THORIN_LNAME(thorin_push) = thorin::push((what), (with))

/**
 * A tagged pointer: first 16 bits is tag (index), remaining 48 bits is the actual pointer.
 * For non-x86_64 there is a fallback implementation.
 */
template<class T, class I = size_t>
class TaggedPtr {
public:
    TaggedPtr() {}
#if defined(__x86_64__) || (_M_X64)
    TaggedPtr(T* ptr, I index)
        : ptr_(reinterpret_cast<int64_t>(ptr))
        , index_(index)
    {}
#else
    TaggedPtr(T* ptr, I index)
        : ptr_(ptr)
        , index_(index)
    {}
#endif

    T* ptr() const { return reinterpret_cast<T*>(ptr_); }
    T* operator->() const { return ptr(); }
    operator T*() const { return ptr(); }
    void index(I index) { index_ = index; }
    I index() const { return index_; }
    bool operator==(TaggedPtr other) const { return this->ptr() == other.ptr() && this->index() == other.index(); }

private:
#if defined(__x86_64__) || (_M_X64)
    int64_t ptr_   : 48; // sign extend to make pointer canonical
    I       index_ : 16;
#else
    T* ptr_;
    I index_;
#endif
};

#if defined(__x86_64__) || (_M_X64)
static_assert(sizeof(TaggedPtr<void*,int>) == 8, "a tagged ptr on x86_64 is supposed to be 8 bytes big");
#endif

//@{ bit fiddling

/// Determines whether @p i is a power of two.
constexpr uint64_t is_power_of_2(uint64_t i) { return ((i != 0) && !(i & (i - 1))); }

constexpr uint64_t log2(uint64_t n, uint64_t p = 0) { return (n <= UINT64_C(1)) ? p : log2(n / UINT64_C(2), p + UINT64_C(1)); }

constexpr uint64_t round_to_power_of_2(uint64_t i) {
    i--;
    i |= i >> UINT64_C( 1);
    i |= i >> UINT64_C( 2);
    i |= i >> UINT64_C( 4);
    i |= i >> UINT64_C( 8);
    i |= i >> UINT64_C(16);
    i |= i >> UINT64_C(32);
    i++;
    return i;
}

inline size_t bitcount(uint64_t v) {
#if defined(__GNUC__) | defined(__clang__)
    return __builtin_popcountll(v);
#elif defined(_MSC_VER)
    return __popcnt64(v);
#else
    // see https://stackoverflow.com/questions/3815165/how-to-implement-bitcount-using-only-bitwise-operators
    auto c = v - ((v >> UINT64_C( 1))      & UINT64_C(0x5555555555555555));
    c =          ((c >> UINT64_C( 2))      & UINT64_C(0x3333333333333333)) + (c & 0xUINT64_C(3333333333333333));
    c =          ((c >> UINT64_C( 4)) + c) & UINT64_C(0x0F0F0F0F0F0F0F0F);
    c =          ((c >> UINT64_C( 8)) + c) & UINT64_C(0x00FF00FF00FF00FF);
    c =          ((c >> UINT64_C(16)) + c) & UINT64_C(0x0000FFFF0000FFFF);
    return       ((c >> UINT64_C(32)) + c) & UINT64_C(0x00000000FFFFFFFF);
#endif
}

//@}

}

#endif
