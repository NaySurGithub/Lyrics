#include "cli/Injector.hpp"

#include "shared/Names.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cwctype>
#include <optional>
#include <string_view>

namespace lyrics::cli::injector {
namespace {

std::wstring lower(std::wstring value) {
    for (auto& c : value) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return value;
}

struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

std::optional<DWORD> find_game() {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.value == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snapshot.value, &entry); ok; ok = Process32NextW(snapshot.value, &entry)) {
        if (_wcsicmp(entry.szExeFile, shared::kGameExecutable) == 0) {
            return entry.th32ProcessID;
        }
    }
    return std::nullopt;
}

std::optional<MODULEENTRY32W> find_module(DWORD process_id, const std::wstring& module_name) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id)};
    if (snapshot.value == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    const std::wstring wanted = lower(module_name);
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Module32FirstW(snapshot.value, &entry); ok; ok = Module32NextW(snapshot.value, &entry)) {
        if (lower(entry.szModule) == wanted) {
            return entry;
        }
    }
    return std::nullopt;
}

bool is_loaded(DWORD process_id, const std::wstring& module_name) {
    return find_module(process_id, module_name).has_value();
}

Result inject_into(DWORD process_id, const std::filesystem::path& dll) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(dll, error)) {
        return {false, "dll not found"};
    }
    const std::wstring path = std::filesystem::absolute(dll, error).wstring();

    Handle process{OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                   PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, process_id)};
    if (process.value == nullptr) {
        return {false, "could not open minecraft (" + std::to_string(GetLastError()) + ")"};
    }

    const SIZE_T size = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process.value, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr || !WriteProcessMemory(process.value, remote, path.c_str(), size, nullptr)) {
        return {false, "could not write the dll path"};
    }

    // wine does not always map kernel32 at the same base in both processes, so the address is rebased onto the
    // game's copy instead of assuming ours lines up
    const HMODULE local_kernel = GetModuleHandleW(L"kernel32.dll");
    const auto remote_kernel = find_module(process_id, L"kernel32.dll");
    if (local_kernel == nullptr || !remote_kernel) {
        VirtualFreeEx(process.value, remote, 0, MEM_RELEASE);
        return {false, "could not find kernel32 in minecraft"};
    }
    const auto local_load = reinterpret_cast<std::uintptr_t>(GetProcAddress(local_kernel, "LoadLibraryW"));
    const std::uintptr_t offset = local_load - reinterpret_cast<std::uintptr_t>(local_kernel);
    auto* load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_kernel->modBaseAddr + offset);
    Handle thread{CreateRemoteThread(process.value, nullptr, 0, load_library, remote, 0, nullptr)};
    if (thread.value == nullptr) {
        VirtualFreeEx(process.value, remote, 0, MEM_RELEASE);
        return {false, "could not start the loader thread"};
    }
    WaitForSingleObject(thread.value, 15000);
    DWORD loaded = 0;
    GetExitCodeThread(thread.value, &loaded);
    VirtualFreeEx(process.value, remote, 0, MEM_RELEASE);
    if (loaded == 0) {
        return {false, "LoadLibrary failed inside minecraft"};
    }
    return {true, "injected"};
}

template <typename T>
bool read_remote(HANDLE process, const std::uint8_t* address, T& out) {
    SIZE_T read = 0;
    return ReadProcessMemory(process, address, &out, sizeof(out), &read) && read == sizeof(out);
}

std::uintptr_t remote_export(HANDLE process, const std::uint8_t* base, std::string_view name) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!read_remote(process, base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !read_remote(process, base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }
    const auto& directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    IMAGE_EXPORT_DIRECTORY exports{};
    if (directory.VirtualAddress == 0 || !read_remote(process, base + directory.VirtualAddress, exports)) {
        return 0;
    }
    for (DWORD index = 0; index < exports.NumberOfNames; ++index) {
        DWORD name_rva = 0;
        WORD ordinal = 0;
        DWORD function_rva = 0;
        char buffer[64]{};
        SIZE_T read = 0;
        if (!read_remote(process, base + exports.AddressOfNames + index * sizeof(DWORD), name_rva) ||
            !ReadProcessMemory(process, base + name_rva, buffer, sizeof(buffer) - 1, &read)) {
            continue;
        }
        if (name != buffer) {
            continue;
        }
        if (read_remote(process, base + exports.AddressOfNameOrdinals + index * sizeof(WORD), ordinal) &&
            read_remote(process, base + exports.AddressOfFunctions + ordinal * sizeof(DWORD), function_rva)) {
            return function_rva;
        }
    }
    return 0;
}

Result uninject_from(DWORD process_id, const std::wstring& module_name) {
    const auto module = find_module(process_id, module_name);
    if (!module) {
        return {false, "the overlay is not injected"};
    }

    Handle process{OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                   PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, process_id)};
    if (process.value == nullptr) {
        return {false, "could not open minecraft (" + std::to_string(GetLastError()) + ")"};
    }
    const std::uintptr_t offset = remote_export(process.value, module->modBaseAddr, shared::kUnloadExport);
    if (offset == 0) {
        return {false, "this overlay build cannot unload"};
    }
    auto* routine = reinterpret_cast<LPTHREAD_START_ROUTINE>(module->modBaseAddr + offset);
    Handle thread{CreateRemoteThread(process.value, nullptr, 0, routine, nullptr, 0, nullptr)};
    if (thread.value == nullptr) {
        return {false, "could not start the unload thread"};
    }
    WaitForSingleObject(thread.value, 5000);

    // shutdown joins the worker thread and waits on the overlay fence; under wine that can take longer than 5s
    for (int attempt = 0; attempt < 150; ++attempt) {
        if (!is_loaded(process_id, module_name)) {
            return {true, "uninjected"};
        }
        Sleep(100);
    }
    return {false, "the overlay did not unload in time"};
}
}

Status status() {
    Status result;
    if (const auto game = find_game()) {
        result.game = *game;
        result.loaded = is_loaded(*game, shared::kDllName);
    }
    return result;
}

Result inject(const std::filesystem::path& dll) {
    const auto game = find_game();
    if (!game) {
        return {false, "minecraft is not running"};
    }
    if (is_loaded(*game, shared::kDllName)) {
        return {false, "already injected"};
    }
    return inject_into(*game, dll);
}

Result uninject() {
    const auto game = find_game();
    if (!game) {
        return {false, "minecraft is not running"};
    }
    return uninject_from(*game, shared::kDllName);
}

}
