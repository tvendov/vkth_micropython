#include <cstddef>

extern "C" {
#include "lvgl/lvgl.h"
#include "py/runtime.h"
}

namespace {

[[noreturn]] void raise_cxx_memory_error()
{
    mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("ThorVG memory allocation failed"));
}

[[noreturn]] void raise_cxx_runtime_error()
{
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ThorVG C++ runtime error"));
}

} // namespace

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
