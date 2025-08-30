#include <luart.h>
#include <windows.h>
#include <winreg.h>
#include <comdef.h>
#include <wbemidl.h>
#include <intrin.h>
#include <string.h>
#include <math.h>


typedef LONG NT_STATUS;
#define NT_SUCCESS(Status) (((NT_STATUS)(Status)) >= 0)

// Global state for CPU usage
static ULARGE_INTEGER prev_idle = {0}, prev_kernel = {0}, prev_user = {0};
static ULARGE_INTEGER* prev_core_idle = nullptr;
static ULARGE_INTEGER* prev_core_kernel = nullptr;
static ULARGE_INTEGER* prev_core_user = nullptr;
static DWORD num_cores = 0;

static ULARGE_INTEGER FiletimeToUlarge(const FILETIME* ft) {
    ULARGE_INTEGER ul;
    ul.LowPart = ft->dwLowDateTime;
    ul.HighPart = ft->dwHighDateTime;
    return ul;
}

LUA_PROPERTY_GET(cpu, usage) {
    FILETIME idle_time, kernel_time, user_time;
    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        lua_pushnil(L);
        return 1;
    }

    ULARGE_INTEGER curr_idle = FiletimeToUlarge(&idle_time);
    ULARGE_INTEGER curr_kernel = FiletimeToUlarge(&kernel_time);
    ULARGE_INTEGER curr_user = FiletimeToUlarge(&user_time);

    ULONGLONG d_idle = curr_idle.QuadPart - prev_idle.QuadPart;
    ULONGLONG d_kernel = curr_kernel.QuadPart - prev_kernel.QuadPart;
    ULONGLONG d_user = curr_user.QuadPart - prev_user.QuadPart;
    ULONGLONG d_total = d_kernel + d_user;

    prev_idle = curr_idle;
    prev_kernel = curr_kernel;
    prev_user = curr_user;

    if (d_total == 0) {
        lua_pushnumber(L, 0.0);
        return 1;
    }

    double usage = 100.0 * (d_total - d_idle) / d_total;
    lua_pushnumber(L, ceil(usage < 0.0 ? 0.0 : usage > 100.0 ? 100.0 : usage));
    return 1;
}

LUA_PROPERTY_GET(cpu, threads) {
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    lua_pushinteger(L, sys_info.dwNumberOfProcessors);
    return 1;
}

LUA_PROPERTY_GET(cpu, name) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        lua_pushnil(L);
        return 1;
    }

    char buffer[256];
    DWORD size = sizeof(buffer);
    if (RegQueryValueExA(key, "ProcessorNameString", nullptr, nullptr, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
        lua_pushstring(L, buffer);
    } else {
        lua_pushnil(L);
    }
    RegCloseKey(key);
    return 1;
}

LUA_PROPERTY_GET(cpu, clock) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        lua_pushnil(L);
        return 1;
    }
    DWORD speed;
    DWORD size = sizeof(speed);
    if (RegQueryValueExA(key, "~MHz", nullptr, nullptr, (LPBYTE)&speed, &size) == ERROR_SUCCESS) {
        lua_pushinteger(L, speed);
    } else {
        lua_pushnil(L);
    }
    RegCloseKey(key);
    return 1;
}

LUA_PROPERTY_GET(cpu, currentclock) {
    LARGE_INTEGER freq;
    if (!QueryPerformanceFrequency(&freq)) {
        lua_pushnil(L);
        return 1;
    }

    unsigned __int64 tsc_start = __rdtsc();
    Sleep(10);
    unsigned __int64 tsc_end = __rdtsc();

    LARGE_INTEGER counter_start, counter_end;
    QueryPerformanceCounter(&counter_start);
    Sleep(10);
    QueryPerformanceCounter(&counter_end);

    double elapsed_time = (double)(counter_end.QuadPart - counter_start.QuadPart) / freq.QuadPart;
    double tsc_freq = (tsc_end - tsc_start) / elapsed_time; 
    double mhz = tsc_freq / 1000000.0; 

    lua_pushnumber(L, mhz);
    return 1;
}

LUA_PROPERTY_GET(cpu, vendor) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        lua_pushnil(L);
        return 1;
    }

    char buffer[256];
    DWORD size = sizeof(buffer);
    if (RegQueryValueExA(key, "VendorIdentifier", nullptr, nullptr, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
        lua_pushstring(L, buffer);
    } else {
        lua_pushnil(L);
    }
    RegCloseKey(key);
    return 1;
}

LUA_PROPERTY_GET(cpu, features) {
    int cpuInfo[4] = {0};
    lua_newtable(L);

    __cpuid(cpuInfo, 0);
    if (cpuInfo[0] < 1)
        return 1;

    __cpuid(cpuInfo, 1);
    if (cpuInfo[3] & (1 << 23)) { lua_pushboolean(L, 1); lua_setfield(L, -2, "MMX"); }
    if (cpuInfo[3] & (1 << 25)) { lua_pushboolean(L, 1); lua_setfield(L, -2, "SSE"); }
    if (cpuInfo[3] & (1 << 26)) { lua_pushboolean(L, 1); lua_setfield(L, -2, "SSE2"); }
    if (cpuInfo[2] & (1 << 0))  { lua_pushboolean(L, 1); lua_setfield(L, -2, "SSE3"); }
    if (cpuInfo[2] & (1 << 9))  { lua_pushboolean(L, 1); lua_setfield(L, -2, "SSSE3"); }
    if (cpuInfo[2] & (1 << 19)) { lua_pushboolean(L, 1); lua_setfield(L, -2, "SSE4_1"); }
    if (cpuInfo[2] & (1 << 20)) { lua_pushboolean(L, 1); lua_setfield(L, -2, "SSE4_2"); }
    if (cpuInfo[2] & (1 << 28)) { lua_pushboolean(L, 1); lua_setfield(L, -2, "AVX"); }

    __cpuid(cpuInfo, 7);
    if (cpuInfo[1] & (1 << 5)) { lua_pushboolean(L, 1); lua_setfield(L, -2, "AVX2"); }

    return 1;
}

// Static arrays for priority class mapping
typedef struct {
    const char* name;
    DWORD value;
} PriorityClassMap;

static const PriorityClassMap cpu_priority_to_string[7] = {
    {"idle", IDLE_PRIORITY_CLASS},
    {"below_normal", BELOW_NORMAL_PRIORITY_CLASS},
    {"normal", NORMAL_PRIORITY_CLASS},
    {"above_normal", ABOVE_NORMAL_PRIORITY_CLASS},
    {"high", HIGH_PRIORITY_CLASS},
    {"unknown", 0}
};

// Lua: cpu.priority -> table {class = string, level = number}
LUA_PROPERTY_GET(cpu, priority) {
HANDLE process = GetCurrentProcess();
    DWORD priority_class = GetPriorityClass(process);
    if (priority_class == 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to get priority class");
        return 2;
    }

    const char* class_str = "unknown";
    for (int i = 0; i < 7; ++i) {
        if (cpu_priority_to_string[i].value == priority_class) {
            class_str = cpu_priority_to_string[i].name;
            break;
        }
    }

    lua_pushstring(L, class_str);
    return 1;
}

LUA_PROPERTY_SET(cpu, priority) {
    const char* class_str = luaL_checkstring(L, -1);

    DWORD priority_class = 0;
    for (int i = 0; cpu_priority_to_string[i].name; i++) {
        if (strcmp(cpu_priority_to_string[i].name, class_str) == 0) {
            priority_class = cpu_priority_to_string[i].value;
            break;
        }
    }

    if (priority_class != 0) {
        SetPriorityClass(GetCurrentProcess(), priority_class);
        SetThreadPriority(GetCurrentThread(), priority_class);

    }
    return 0;
}

LUA_PROPERTY_GET(cpu, cores) {
    DWORD buffer_size = 0;
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &buffer_size) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        lua_pushnil(L);
        return 1;
    }

    char* buffer = (char*)malloc(buffer_size);
    if (!buffer) {
        lua_pushnil(L);
        return 1;
    }

    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)buffer;
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &buffer_size)) {
        free(buffer);
        lua_pushnil(L);
        return 1;
    }

    DWORD offset = 0;
    DWORD core_count = 0;
    while (offset < buffer_size) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* current = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(buffer + offset);
        if (current->Relationship == RelationProcessorCore)
            core_count++;
        offset += current->Size;
    }

    free(buffer);
    lua_pushinteger(L, core_count);
    return 1;
}

LUA_PROPERTY_GET(cpu, cache) {
    DWORD buffer_size = 0;
    GetLogicalProcessorInformation(nullptr, &buffer_size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        lua_pushnil(L);
        return 1;
    }

    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(buffer_size);
    if (!info) {
        lua_pushnil(L);
        return 1;
    }

    if (!GetLogicalProcessorInformation(info, &buffer_size)) {
        free(info);
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    int cache_levels[3] = {0};
    DWORD count = buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    for (DWORD i = 0; i < count; ++i) {
        if (info[i].Relationship == RelationCache) {
            CACHE_DESCRIPTOR* cache = &info[i].Cache;
            const char* level_key;
            int level_idx;

            if (cache->Level == 1) { level_key = "L1"; level_idx = 0; }
            else if (cache->Level == 2) { level_key = "L2"; level_idx = 1; }
            else if (cache->Level == 3) { level_key = "L3"; level_idx = 2; }
            else continue;

            if (cache_levels[level_idx]) continue;

            lua_pushinteger(L, cache->Size / 1024); // Bytes to KB
            lua_setfield(L, -2, level_key);

            cache_levels[level_idx] = 1;
        }
    }

    free(info);
    return 1;
}

LUA_METHOD(cpu, prefetch) {
    size_t len;
    const char* str = lua_tolstring(L, 1, &len);
    if (str && len > 63)
        for (size_t i = 0; i < len; i += 64)
            _mm_prefetch(str + i, _MM_HINT_T0);
    return 0;
}

LUA_METHOD(cpu, finalize) {
    if (prev_core_idle) free(prev_core_idle);
    if (prev_core_kernel) free(prev_core_kernel);
    if (prev_core_user) free(prev_core_user);
    prev_core_idle = prev_core_kernel = prev_core_user = nullptr;
    num_cores = 0;
    return 0;
}

// Register properties
MODULE_PROPERTIES(cpu)
    READONLY_PROPERTY(cpu, usage)
    READONLY_PROPERTY(cpu, cores)
    READONLY_PROPERTY(cpu, threads)
    READONLY_PROPERTY(cpu, name)
    READONLY_PROPERTY(cpu, clock)
    READONLY_PROPERTY(cpu, currentclock)
    READONLY_PROPERTY(cpu, vendor)
    READONLY_PROPERTY(cpu, features)
    READWRITE_PROPERTY(cpu, priority)
    READONLY_PROPERTY(cpu, cache)
END

// Register functions
MODULE_FUNCTIONS(cpu)
END

// Module registration
extern "C" __declspec(dllexport) int luaopen_cpu(lua_State* L) {
    lua_regmodulefinalize(L, cpu);
    return 1;
}