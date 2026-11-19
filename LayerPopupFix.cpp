// LayerPopupFix: a proxy DLL (dwmapi.dll by default) that stops the DOOM: The Dark Ages launcher
// (idTechLauncher.exe) from showing its "unsupported Vulkan Layers" pop-up for ReShade.
//
// How it works: the launcher looks up vkEnumerateInstanceLayerProperties at runtime
// (through vkGetInstanceProcAddr or GetProcAddress) and shows the pop-up when the list
// contains VK_LAYER_reshade. Inside idTechLauncher.exe only, this DLL patches the
// launcher's own import table for those two lookup functions and hands back a wrapper
// that removes VK_LAYER_reshade from the list.
//
// It does nothing in any other process (including DOOMTheDarkAges.exe), so ReShade
// still loads in the game and nothing here runs alongside the Kaibz mod's or ReShade's
// startup in the game process.
//
// Every export is forwarded to the real DLL of the same name in the build machine's
// %SystemRoot%\System32. build.cmd generates that forwarder list per DLL name with
// gen_exports.ps1 and force-includes it (/FI), so this file has no #include for it.
//
// Log: LayerPopupFix.log next to the exe, rewritten on each launcher start.

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Minimal Vulkan definitions so no SDK is needed.
using VkResult = int32_t;
constexpr VkResult VK_SUCCESS = 0;
constexpr VkResult VK_INCOMPLETE = 5;
constexpr VkResult VK_ERROR_INITIALIZATION_FAILED = -3;

struct VkLayerProperties {
    char layerName[256];
    uint32_t specVersion;
    uint32_t implementationVersion;
    char description[256];
};

using PFN_EnumLayers = VkResult (*)(uint32_t*, VkLayerProperties*);
using PFN_GetInstanceProcAddr = void (*(*)(void*, const char*))();
using PFN_GetProcAddress = FARPROC(WINAPI*)(HMODULE, LPCSTR);

constexpr char kEnumLayersName[] = "vkEnumerateInstanceLayerProperties";
constexpr char kHiddenLayerPrefix[] = "VK_LAYER_reshade";

PFN_EnumLayers g_realEnumLayers = nullptr;
PFN_GetInstanceProcAddr g_realGetInstanceProcAddr = nullptr;
PFN_GetProcAddress g_realGetProcAddress = nullptr;
HANDLE g_log = INVALID_HANDLE_VALUE;
SRWLOCK g_logLock = SRWLOCK_INIT;

void Log(const char* fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char buf[512];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = wsprintfA(buf, "%02u:%02u:%02u.%03u  ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    n += wvsprintfA(buf + n, fmt, args);
    va_end(args);
    buf[n++] = '\r';
    buf[n++] = '\n';
    AcquireSRWLockExclusive(&g_logLock);
    DWORD written;
    WriteFile(g_log, buf, n, &written, nullptr);
    ReleaseSRWLockExclusive(&g_logLock);
}

bool IsHiddenLayer(const char* name) {
    return _strnicmp(name, kHiddenLayerPrefix, sizeof(kHiddenLayerPrefix) - 1) == 0;
}

// Same contract as vkEnumerateInstanceLayerProperties, minus the hidden layer.
VkResult HookEnumLayers(uint32_t* pCount, VkLayerProperties* pProps) {
    PFN_EnumLayers real = g_realEnumLayers;
    if (!real || !pCount) return real ? real(pCount, pProps) : VK_ERROR_INITIALIZATION_FAILED;

    std::vector<VkLayerProperties> all;
    VkResult r;
    do {  // The layer count can change between calls; retry on VK_INCOMPLETE as the spec suggests.
        uint32_t n = 0;
        r = real(&n, nullptr);
        if (r < 0) return r;
        all.resize(n);
        r = n ? real(&n, all.data()) : VK_SUCCESS;
        if (r < 0) return r;
        all.resize(n);
    } while (r == VK_INCOMPLETE);

    std::vector<VkLayerProperties> kept;
    kept.reserve(all.size());
    for (const auto& layer : all) {
        if (IsHiddenLayer(layer.layerName)) {
            Log("hid layer %s from the launcher", layer.layerName);
        } else {
            kept.push_back(layer);
        }
    }

    const uint32_t total = static_cast<uint32_t>(kept.size());
    if (!pProps) {
        *pCount = total;
        return VK_SUCCESS;
    }
    const uint32_t copied = *pCount < total ? *pCount : total;
    if (copied) memcpy(pProps, kept.data(), copied * sizeof(VkLayerProperties));
    *pCount = copied;
    return copied < total ? VK_INCOMPLETE : VK_SUCCESS;
}

// Wraps a freshly looked-up vkEnumerateInstanceLayerProperties.
void* WrapEnumLayers(void* real, const char* via) {
    if (!real || real == reinterpret_cast<void*>(&HookEnumLayers)) return real;
    g_realEnumLayers = reinterpret_cast<PFN_EnumLayers>(real);
    Log("launcher looked up %s via %s; wrapped it", kEnumLayersName, via);
    return reinterpret_cast<void*>(&HookEnumLayers);
}

void (*HookGetInstanceProcAddr(void* instance, const char* name))() {
    auto fn = g_realGetInstanceProcAddr(instance, name);
    if (!instance && name && strcmp(name, kEnumLayersName) == 0) {
        return reinterpret_cast<void (*)()>(WrapEnumLayers(reinterpret_cast<void*>(fn), "vkGetInstanceProcAddr"));
    }
    return fn;
}

FARPROC WINAPI HookGetProcAddress(HMODULE module, LPCSTR name) {
    FARPROC fn = g_realGetProcAddress(module, name);
    if (IS_INTRESOURCE(name) || strcmp(name, kEnumLayersName) != 0) return fn;
    return reinterpret_cast<FARPROC>(WrapEnumLayers(reinterpret_cast<void*>(fn), "GetProcAddress"));
}

// Replaces every import of `func` (from a DLL whose name starts with one of `dllPrefixes`)
// in the main exe's import table. Returns the previous target of the first one patched.
void* PatchImport(const char* const* dllPrefixes, size_t prefixCount, const char* func, void* hook, int* patched) {
    auto base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;

    void* original = nullptr;
    for (auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
        const char* dll = reinterpret_cast<const char*>(base + desc->Name);
        bool match = false;
        for (size_t i = 0; i < prefixCount && !match; ++i) {
            match = _strnicmp(dll, dllPrefixes[i], strlen(dllPrefixes[i])) == 0;
        }
        if (!match || !desc->OriginalFirstThunk) continue;

        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(byName->Name), func) != 0) continue;

            void* previous = reinterpret_cast<void*>(slots->u1.Function);
            if (previous == hook) continue;
            DWORD oldProtect;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect)) continue;
            slots->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
            VirtualProtect(&slots->u1.Function, sizeof(void*), oldProtect, &oldProtect);
            if (!original) original = previous;
            ++*patched;
        }
    }
    return original;
}

bool IsLauncherProcess(wchar_t* exePath, DWORD size) {
    DWORD len = GetModuleFileNameW(nullptr, exePath, size);
    if (!len || len >= size) return false;
    const wchar_t* file = exePath;
    for (const wchar_t* p = exePath; *p; ++p) {
        if (*p == L'\\' || *p == L'/') file = p + 1;
    }
    return _wcsicmp(file, L"idTechLauncher.exe") == 0;
}

void OpenLog(const wchar_t* exePath) {
    wchar_t path[MAX_PATH];
    lstrcpynW(path, exePath, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash || (slash - path) + 18 >= MAX_PATH) return;
    lstrcpyW(slash + 1, L"LayerPopupFix.log");
    g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Install() {
    wchar_t exePath[MAX_PATH];
    if (!IsLauncherProcess(exePath, MAX_PATH)) return;
    OpenLog(exePath);
    Log("LayerPopupFix loaded into idTechLauncher.exe");

    int patched = 0;
    const char* vulkanDlls[] = {"vulkan-1.dll"};
    g_realGetInstanceProcAddr = reinterpret_cast<PFN_GetInstanceProcAddr>(
        PatchImport(vulkanDlls, 1, "vkGetInstanceProcAddr", reinterpret_cast<void*>(&HookGetInstanceProcAddr), &patched));
    Log("vkGetInstanceProcAddr import: %s", g_realGetInstanceProcAddr ? "hooked" : "not found");

    int gpaPatched = 0;
    const char* loaderDlls[] = {"kernel32.dll", "api-ms-win-core-libraryloader-"};
    g_realGetProcAddress = reinterpret_cast<PFN_GetProcAddress>(
        PatchImport(loaderDlls, 2, "GetProcAddress", reinterpret_cast<void*>(&HookGetProcAddress), &gpaPatched));
    if (!g_realGetProcAddress) g_realGetProcAddress = &GetProcAddress;
    Log("GetProcAddress imports hooked: %d", gpaPatched);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        Install();
    }
    return TRUE;
}
