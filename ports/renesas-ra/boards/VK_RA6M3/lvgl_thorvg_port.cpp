#include <cstddef>
#include <cstdint>

// Instantiate std::string with this target's exception-free C++ flags instead
// of pulling the exception-enabled prebuilt libstdc++ string-inst object.
#undef _GLIBCXX_EXTERN_TEMPLATE
#define _GLIBCXX_EXTERN_TEMPLATE -1
#include <string>

extern "C" {
#include "lvgl/lvgl.h"
#include "py/runtime.h"
}

namespace {

std::uint32_t c_rand_state = 1u;

[[noreturn]] void raise_cxx_memory_error()
{
    mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("ThorVG memory allocation failed"));
}

[[noreturn]] void raise_cxx_runtime_error()
{
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ThorVG C++ runtime error"));
}

} // namespace

extern "C" void srand(unsigned int seed)
{
    c_rand_state = seed;
}

extern "C" int rand(void)
{
    c_rand_state = c_rand_state * 1664525u + 1013904223u;
    return static_cast<int>(c_rand_state & 0x7fffffffu);
}

void * operator new(std::size_t size)
{
    void * ptr = lv_malloc(size);
    if(ptr == nullptr) raise_cxx_memory_error();
    return ptr;
}

void * operator new[](std::size_t size)
{
    void * ptr = lv_malloc(size);
    if(ptr == nullptr) raise_cxx_memory_error();
    return ptr;
}

void operator delete(void * ptr) noexcept
{
    lv_free(ptr);
}

void operator delete[](void * ptr) noexcept
{
    lv_free(ptr);
}

void operator delete(void * ptr, std::size_t) noexcept
{
    lv_free(ptr);
}

void operator delete[](void * ptr, std::size_t) noexcept
{
    lv_free(ptr);
}

extern "C" [[noreturn]] void __cxa_pure_virtual(void)
{
    raise_cxx_runtime_error();
}

extern "C" [[noreturn]] void __cxa_deleted_virtual(void)
{
    raise_cxx_runtime_error();
}

namespace std {

[[noreturn]] void __throw_bad_alloc() { raise_cxx_memory_error(); }
[[noreturn]] void __throw_bad_array_new_length() { raise_cxx_memory_error(); }
[[noreturn]] void __throw_bad_cast() { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_bad_exception() { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_bad_function_call() { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_bad_typeid() { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_domain_error(const char *) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_future_error(int) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_invalid_argument(const char *) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_ios_failure(const char *) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_ios_failure(const char *, int) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_length_error(const char *) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_logic_error(const char *) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_out_of_range(const char *) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_out_of_range_fmt(const char *, ...) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_overflow_error(const char *) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_range_error(const char *) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_runtime_error(const char *) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_system_error(int) { raise_cxx_runtime_error(); }
[[noreturn]] void __throw_underflow_error(const char *) { raise_cxx_runtime_error(); }

} // namespace std

template class std::basic_string<char>;
