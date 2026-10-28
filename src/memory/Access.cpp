#include "memory/Access.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// a byte copy with no stack frame; a fault inside it turns into "return 0" through the handler below.
// ReadProcessMemory does the same job, but under wine every call is a round trip to the wine server
extern "C" int lyrics_safe_copy(void* destination, const void* source, std::size_t size);
extern "C" char lyrics_safe_copy_end;

asm(R"(
    .text
    .globl lyrics_safe_copy
lyrics_safe_copy:
    test %r8, %r8
    jz 2f
1:
    movb (%rdx), %al
    movb %al, (%rcx)
    inc %rdx
    inc %rcx
    dec %r8
    jnz 1b
2:
    mov $1, %eax
    ret
    .globl lyrics_safe_copy_end
lyrics_safe_copy_end:
    nop
)");

namespace lyrics::memory {
namespace {

LONG CALLBACK on_fault(EXCEPTION_POINTERS* info) {
    const auto code = info->ExceptionRecord->ExceptionCode;
    CONTEXT* context = info->ContextRecord;
    const auto start = reinterpret_cast<DWORD64>(&lyrics_safe_copy);
    const auto end = reinterpret_cast<DWORD64>(&lyrics_safe_copy_end);
    if ((code != EXCEPTION_ACCESS_VIOLATION && code != STATUS_GUARD_PAGE_VIOLATION) || context->Rip < start ||
        context->Rip >= end) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // the copy never touches the stack, so rsp still points at its return address
    context->Rax = 0;
    context->Rip = *reinterpret_cast<DWORD64*>(context->Rsp);
    context->Rsp += 8;
    return EXCEPTION_CONTINUE_EXECUTION;
}

struct Guard {
    PVOID handler = AddVectoredExceptionHandler(1, &on_fault);
    ~Guard() {
        if (handler != nullptr) {
            RemoveVectoredExceptionHandler(handler);
        }
    }
};

const Guard guard;

}

bool read(std::uintptr_t address, void* out, std::size_t size) {
    if (address < 0x10000) {
        return false;
    }
    if (guard.handler == nullptr) {
        SIZE_T done = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, &done) &&
            done == size;
    }
    return lyrics_safe_copy(out, reinterpret_cast<const void*>(address), size) != 0;
}

std::uintptr_t image_base() {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
}

}
