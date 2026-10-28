#include "render/Overlay.hpp"

#include "game/Camera.hpp"
#include "game/Player.hpp"
#include "render/Covers.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <safetyhook.hpp>

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

namespace lyrics::render {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kSrvDescriptorCount = 64;
// submissions kept in flight apart from the swap chain: tying them to its 2-3 back buffers made the cpu wait on the
// previous frame's gpu work every frame, which under D3DMetal costs a lot of fps
constexpr std::size_t kInFlight = 4;
constexpr UINT kPointerFirst = 0x0241;
constexpr UINT kPointerLast = 0x0251;
constexpr USHORT kMouseReleases = RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_UP |
    RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP;

struct Frame {
    ComPtr<ID3D12Resource> back_buffer;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
};

struct Submission {
    ComPtr<ID3D12CommandAllocator> allocator;
    UINT64 fence_value = 0;
};

struct State {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    ComPtr<ID3D12DescriptorHeap> srv_heap;
    ComPtr<ID3D12GraphicsCommandList> command_list;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;
    UINT64 fence_counter = 0;
    IDXGISwapChain3* swap_chain = nullptr;
    std::vector<Frame> frames;
    std::array<Submission, kInFlight> submissions;
    std::size_t submission_index = 0;
    UINT srv_next = 0;
    HWND window = nullptr;
    WNDPROC original_wndproc = nullptr;
    bool imgui_ready = false;
};

State state;
std::mutex state_mutex;
FrameCallback on_frame = nullptr;
std::atomic_bool menu_visible = false;
std::atomic_int menu_key = VK_RSHIFT;

safetyhook::InlineHook present_hook;
safetyhook::InlineHook resize_buffers_hook;
safetyhook::InlineHook resize_buffers1_hook;
safetyhook::InlineHook execute_command_lists_hook;
safetyhook::InlineHook clip_cursor_hook;
safetyhook::InlineHook set_cursor_pos_hook;
safetyhook::InlineHook raw_input_hook;
safetyhook::InlineHook show_cursor_hook;
safetyhook::InlineHook set_cursor_hook;

// ---- cursor: the game locks and hides it while playing, the search needs it back ----

std::mutex cursor_mutex;
bool game_clipped = false;
RECT game_clip{};
std::atomic_bool cursor_pending{false};
int held_shows = 0;
int cursor_shows = 0;
HCURSOR game_cursor = nullptr;
bool game_cursor_known = false;

bool called_by_game(void* return_address) {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(return_address), &module) &&
        module == GetModuleHandleW(nullptr);
}

BOOL WINAPI clip_cursor(const RECT* rect) {
    {
        std::lock_guard lock(cursor_mutex);
        game_clipped = rect != nullptr;
        if (rect != nullptr) {
            game_clip = *rect;
        }
    }
    return clip_cursor_hook.stdcall<BOOL>(is_menu_visible() ? nullptr : rect);
}

BOOL WINAPI set_cursor_pos(int x, int y) {
    return is_menu_visible() ? TRUE : set_cursor_pos_hook.stdcall<BOOL>(x, y);
}

int WINAPI show_cursor(BOOL show) {
    if (is_menu_visible() && called_by_game(__builtin_return_address(0))) {
        held_shows += show ? 1 : -1;
        return show ? 0 : -1;
    }
    return show_cursor_hook.stdcall<int>(show);
}

HCURSOR WINAPI set_cursor(HCURSOR cursor) {
    if (called_by_game(__builtin_return_address(0))) {
        game_cursor = cursor;
        game_cursor_known = true;
        if (is_menu_visible()) {
            return cursor;
        }
    }
    return set_cursor_hook.stdcall<HCURSOR>(cursor);
}

bool is_menu_key(USHORT virtual_key) {
    const int wanted = menu_key.load();
    // raw input reports both shifts as VK_SHIFT, the scancode tells them apart
    return virtual_key == wanted || (virtual_key == VK_SHIFT && (wanted == VK_LSHIFT || wanted == VK_RSHIFT));
}

// the game reads its mouse and keys through raw input; with the search open it gets nothing but releases
UINT WINAPI get_raw_input_data(HRAWINPUT input, UINT command, LPVOID data, PUINT size, UINT header_size) {
    const UINT result = raw_input_hook.stdcall<UINT>(input, command, data, size, header_size);
    if (command != RID_INPUT || data == nullptr || result == static_cast<UINT>(-1) || result < sizeof(RAWINPUTHEADER)) {
        return result;
    }
    auto* raw = static_cast<RAWINPUT*>(data);
    if (raw->header.dwType == RIM_TYPEMOUSE && result >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)) {
        if (is_menu_visible()) {
            raw->data.mouse.lLastX = 0;
            raw->data.mouse.lLastY = 0;
            raw->data.mouse.usButtonFlags &= kMouseReleases;
            raw->data.mouse.usButtonData = 0;
            raw->data.mouse.ulRawButtons = 0;
        }
    } else if (raw->header.dwType == RIM_TYPEKEYBOARD && result >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) {
        const bool release = (raw->data.keyboard.Flags & RI_KEY_BREAK) != 0;
        const bool right_shift = raw->data.keyboard.MakeCode == 0x36;
        const bool shift_matches = raw->data.keyboard.VKey != VK_SHIFT || (menu_key.load() == VK_RSHIFT) == right_shift;
        if ((is_menu_key(raw->data.keyboard.VKey) && shift_matches) || (is_menu_visible() && !release)) {
            raw->data.keyboard.VKey = 0xFF;
            raw->data.keyboard.MakeCode = 0;
            raw->data.keyboard.Message = 0;
        }
    }
    return result;
}

void sync_cursor_visibility() {
    if (!cursor_pending.exchange(false)) {
        return;
    }
    if (is_menu_visible()) {
        held_shows = 0;
        for (cursor_shows = 0; cursor_shows < 16 && show_cursor_hook.stdcall<int>(TRUE) < 0;) {
            ++cursor_shows;
        }
        ++cursor_shows;
        set_cursor_hook.stdcall<HCURSOR>(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }
    for (; cursor_shows > 0; --cursor_shows) {
        show_cursor_hook.stdcall<int>(FALSE);
    }
    for (; held_shows > 0; --held_shows) {
        show_cursor_hook.stdcall<int>(TRUE);
    }
    for (; held_shows < 0; ++held_shows) {
        show_cursor_hook.stdcall<int>(FALSE);
    }
    if (game_cursor_known) {
        set_cursor_hook.stdcall<HCURSOR>(game_cursor);
    }
}

// ---- window messages ----

WPARAM sided_key(UINT message, WPARAM wparam, LPARAM lparam) {
    const bool key = message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
    if (!key) {
        return wparam;
    }
    const bool extended = (lparam & (1 << 24)) != 0;
    if (wparam == VK_SHIFT) {
        return ((lparam >> 16) & 0xFF) == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    }
    if (wparam == VK_CONTROL) {
        return extended ? VK_RCONTROL : VK_LCONTROL;
    }
    if (wparam == VK_MENU) {
        return extended ? VK_RMENU : VK_LMENU;
    }
    return wparam;
}

LRESULT CALLBACK wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    sync_cursor_visibility();
    const WPARAM key = sided_key(message, wparam, lparam);
    const bool keyboard = message >= WM_KEYFIRST && message <= WM_KEYLAST;
    if (keyboard && static_cast<int>(key) == menu_key.load()) {
        if (message == WM_KEYDOWN && (lparam & (1 << 30)) == 0 && (is_menu_visible() || game::player::in_world())) {
            set_menu_visible(!is_menu_visible());
        }
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE && is_menu_visible()) {
        set_menu_visible(false);
        return 0;
    }
    if (is_menu_visible() && message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    }
    if (state.imgui_ready && is_menu_visible()) {
        ImGui_ImplWin32_WndProcHandler(window, message, key, lparam);
        if (message == WM_INPUT) {
            return DefWindowProcW(window, message, wparam, lparam);
        }
        const bool mouse = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
        const bool pointer = message >= kPointerFirst && message <= kPointerLast && message != WM_POINTERUP;
        const bool release = message == WM_KEYUP || message == WM_SYSKEYUP || message == WM_LBUTTONUP ||
            message == WM_RBUTTONUP || message == WM_MBUTTONUP || message == WM_XBUTTONUP;
        if ((mouse || keyboard || pointer) && !release) {
            return 0;
        }
    }
    return CallWindowProcW(state.original_wndproc, window, message, wparam, lparam);
}

// ---- d3d12 ----

void wait_for_fence(UINT64 value) {
    if (state.fence != nullptr && state.fence->GetCompletedValue() < value) {
        state.fence->SetEventOnCompletion(value, state.fence_event);
        WaitForSingleObject(state.fence_event, 1000);
    }
}

void release_frames() {
    wait_for_fence(state.fence_counter);
    for (auto& frame : state.frames) {
        frame.back_buffer.Reset();
    }
    state.swap_chain = nullptr;
}

bool create_frames(IDXGISwapChain3* swap_chain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap_chain->GetDesc(&desc)) || desc.BufferCount == 0) {
        return false;
    }
    if (state.frames.size() != desc.BufferCount) {
        state.frames.clear();
        state.frames.resize(desc.BufferCount);
        state.rtv_heap.Reset();
        D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
        rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_desc.NumDescriptors = desc.BufferCount;
        if (FAILED(state.device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&state.rtv_heap)))) {
            return false;
        }
    }
    const UINT rtv_size = state.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = state.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < desc.BufferCount; ++index) {
        auto& frame = state.frames[index];
        if (FAILED(swap_chain->GetBuffer(index, IID_PPV_ARGS(&frame.back_buffer)))) {
            return false;
        }
        frame.rtv = rtv;
        state.device->CreateRenderTargetView(frame.back_buffer.Get(), nullptr, rtv);
        rtv.ptr += rtv_size;
    }
    state.swap_chain = swap_chain;
    return true;
}

void allocate_srv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    const UINT size = state.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const UINT index = state.srv_next < kSrvDescriptorCount ? state.srv_next++ : kSrvDescriptorCount - 1;
    *cpu = state.srv_heap->GetCPUDescriptorHandleForHeapStart();
    *gpu = state.srv_heap->GetGPUDescriptorHandleForHeapStart();
    cpu->ptr += static_cast<SIZE_T>(index) * size;
    gpu->ptr += static_cast<UINT64>(index) * size;
}

void free_srv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {
}

bool init_imgui(IDXGISwapChain3* swap_chain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&state.device))) || FAILED(swap_chain->GetDesc(&desc)) ||
        !create_frames(swap_chain)) {
        return false;
    }
    for (auto& submission : state.submissions) {
        if (FAILED(state.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        IID_PPV_ARGS(&submission.allocator)))) {
            return false;
        }
    }
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = kSrvDescriptorCount;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(state.device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&state.srv_heap))) ||
        FAILED(state.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.submissions[0].allocator.Get(),
                                               nullptr, IID_PPV_ARGS(&state.command_list))) ||
        FAILED(state.command_list->Close()) ||
        FAILED(state.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&state.fence)))) {
        return false;
    }
    state.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    state.window = desc.OutputWindow;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    covers::attach(io.Fonts);

    ImGui_ImplDX12_InitInfo info;
    info.Device = state.device.Get();
    info.CommandQueue = state.queue.Get();
    info.NumFramesInFlight = static_cast<int>(kInFlight);
    info.RTVFormat = desc.BufferDesc.Format;
    info.SrvDescriptorHeap = state.srv_heap.Get();
    info.SrvDescriptorAllocFn = allocate_srv;
    info.SrvDescriptorFreeFn = free_srv;
    if (!ImGui_ImplWin32_Init(state.window) || !ImGui_ImplDX12_Init(&info)) {
        return false;
    }
    state.original_wndproc =
        reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndproc)));
    state.imgui_ready = true;
    return true;
}

void render_frame(IDXGISwapChain3* swap_chain) {
    if (!state.imgui_ready && (state.queue == nullptr || !init_imgui(swap_chain))) {
        return;
    }
    if (swap_chain != state.swap_chain) {
        release_frames();
        if (!create_frames(swap_chain)) {
            return;
        }
    }
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    game::camera::update();
    if (on_frame != nullptr) {
        on_frame();
    }
    ImGui::Render();
    // nothing drawn: no submission at all
    if (ImGui::GetDrawData() == nullptr || ImGui::GetDrawData()->TotalVtxCount == 0) {
        return;
    }

    auto& frame = state.frames[swap_chain->GetCurrentBackBufferIndex() % state.frames.size()];
    auto& submission = state.submissions[state.submission_index++ % kInFlight];
    wait_for_fence(submission.fence_value);
    submission.allocator->Reset();

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frame.back_buffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12DescriptorHeap* heaps[] = {state.srv_heap.Get()};
    state.command_list->Reset(submission.allocator.Get(), nullptr);
    state.command_list->ResourceBarrier(1, &barrier);
    state.command_list->OMSetRenderTargets(1, &frame.rtv, FALSE, nullptr);
    state.command_list->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), state.command_list.Get());
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    state.command_list->ResourceBarrier(1, &barrier);
    state.command_list->Close();

    ID3D12CommandList* lists[] = {state.command_list.Get()};
    execute_command_lists_hook.call<void>(state.queue.Get(), 1u, lists);
    state.queue->Signal(state.fence.Get(), ++state.fence_counter);
    submission.fence_value = state.fence_counter;
}

HRESULT STDMETHODCALLTYPE present(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags) {
    {
        std::lock_guard lock(state_mutex);
        render_frame(swap_chain);
    }
    return present_hook.call<HRESULT>(swap_chain, sync_interval, flags);
}

HRESULT STDMETHODCALLTYPE resize_buffers(IDXGISwapChain3* swap_chain, UINT count, UINT width, UINT height,
                                         DXGI_FORMAT format, UINT flags) {
    {
        std::lock_guard lock(state_mutex);
        release_frames();
    }
    return resize_buffers_hook.call<HRESULT>(swap_chain, count, width, height, format, flags);
}

HRESULT STDMETHODCALLTYPE resize_buffers1(IDXGISwapChain3* swap_chain, UINT count, UINT width, UINT height,
                                          DXGI_FORMAT format, UINT flags, const UINT* node_masks,
                                          IUnknown* const* queues) {
    {
        std::lock_guard lock(state_mutex);
        release_frames();
    }
    return resize_buffers1_hook.call<HRESULT>(swap_chain, count, width, height, format, flags, node_masks, queues);
}

// the overlay has to submit on the queue that presents, and the only way to learn it is to watch the game use it
void STDMETHODCALLTYPE execute_command_lists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
    if (state.queue == nullptr && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        std::lock_guard lock(state_mutex);
        if (state.queue == nullptr) {
            state.queue = queue;
        }
    }
    execute_command_lists_hook.call<void>(queue, count, lists);
}

void* vtable_entry(void* object, std::size_t index) {
    return (*reinterpret_cast<void***>(object))[index];
}

// a throwaway device and swap chain only to read the vtables every d3d12 swap chain and queue share
bool install_d3d_hooks() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"LyricsProbe";
    RegisterClassExW(&window_class);
    HWND probe = CreateWindowExW(0, window_class.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                                 nullptr, window_class.hInstance, nullptr);
    if (probe == nullptr) {
        return false;
    }
    bool installed = false;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain1> swap_chain;
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.Width = 64;
    swap_desc.Height = 64;
    swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = 2;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) &&
        SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) &&
        SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(), probe, &swap_desc, nullptr, nullptr, &swap_chain))) {
        present_hook = safetyhook::create_inline(vtable_entry(swap_chain.Get(), 8), reinterpret_cast<void*>(&present));
        resize_buffers_hook =
            safetyhook::create_inline(vtable_entry(swap_chain.Get(), 13), reinterpret_cast<void*>(&resize_buffers));
        resize_buffers1_hook =
            safetyhook::create_inline(vtable_entry(swap_chain.Get(), 39), reinterpret_cast<void*>(&resize_buffers1));
        execute_command_lists_hook =
            safetyhook::create_inline(vtable_entry(queue.Get(), 10), reinterpret_cast<void*>(&execute_command_lists));
        installed = present_hook && resize_buffers_hook && resize_buffers1_hook && execute_command_lists_hook;
    }
    swap_chain.Reset();
    queue.Reset();
    device.Reset();
    factory.Reset();
    DestroyWindow(probe);
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    return installed;
}

template <typename Function>
safetyhook::InlineHook hook_export(HMODULE module, const char* name, Function* replacement) {
    return safetyhook::create_inline(reinterpret_cast<void*>(GetProcAddress(module, name)),
                                     reinterpret_cast<void*>(replacement));
}

}

bool init(FrameCallback frame) {
    on_frame = frame;
    if (!install_d3d_hooks()) {
        return false;
    }
    RECT current{};
    if (GetClipCursor(&current)) {
        std::lock_guard lock(cursor_mutex);
        game_clipped = current.right - current.left < GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
            current.bottom - current.top < GetSystemMetrics(SM_CYVIRTUALSCREEN);
        game_clip = current;
    }
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        clip_cursor_hook = hook_export(user32, "ClipCursor", &clip_cursor);
        set_cursor_pos_hook = hook_export(user32, "SetCursorPos", &set_cursor_pos);
        raw_input_hook = hook_export(user32, "GetRawInputData", &get_raw_input_data);
        show_cursor_hook = hook_export(user32, "ShowCursor", &show_cursor);
        set_cursor_hook = hook_export(user32, "SetCursor", &set_cursor);
    }
    return true;
}

void shutdown() {
    set_menu_visible(false);
    present_hook = {};
    resize_buffers_hook = {};
    resize_buffers1_hook = {};
    execute_command_lists_hook = {};
    clip_cursor_hook = {};
    set_cursor_pos_hook = {};
    raw_input_hook = {};
    show_cursor_hook = {};
    set_cursor_hook = {};

    std::lock_guard lock(state_mutex);
    if (state.original_wndproc != nullptr) {
        SetWindowLongPtrW(state.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(state.original_wndproc));
    }
    // a message already inside wndproc has to leave it before the code goes away
    Sleep(100);
    if (state.imgui_ready) {
        wait_for_fence(state.fence_counter);
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    if (state.fence_event != nullptr) {
        CloseHandle(state.fence_event);
    }
    state = {};
    on_frame = nullptr;
}

bool is_menu_visible() {
    return menu_visible.load(std::memory_order_acquire);
}

void set_menu_visible(bool visible) {
    if (menu_visible.exchange(visible, std::memory_order_acq_rel) == visible || !clip_cursor_hook) {
        return;
    }
    cursor_pending.store(true);
    if (visible) {
        clip_cursor_hook.stdcall<BOOL>(nullptr);
        if (state.window != nullptr) {
            PostMessageW(state.window, WM_NULL, 0, 0);
        }
        return;
    }
    {
        std::lock_guard lock(cursor_mutex);
        clip_cursor_hook.stdcall<BOOL>(game_clipped ? &game_clip : nullptr);
    }
    if (state.window != nullptr) {
        PostMessageW(state.window, WM_ACTIVATE, MAKEWPARAM(WA_CLICKACTIVE, 0), 0);
        PostMessageW(state.window, WM_SETFOCUS, 0, 0);
    }
}

void set_menu_key(int key) {
    menu_key.store(key);
}

}
