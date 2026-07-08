// game-reader.dll — injected DLL for game data snapshot shared memory
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>

#pragma pack(push, 1)
struct CameraData {
    float loc_x, loc_y, loc_z, rot_x, rot_y, rot_z, fov;
};
struct PlayerData {
    uint64_t pawn_addr, ps_addr;
    float pos_x, pos_y, pos_z, health, shield;
    uint8_t is_hunter, is_survivor, is_enemy, is_local, is_spectating, is_invincible;
    float rot_x, rot_y, rot_z;
    char role_name[32];
    uint8_t _pad[58];
};
struct GameSnapshot {
    uint64_t timestamp;
    CameraData camera;
    int32_t player_count;
    PlayerData players[64];
};
#pragma pack(pop)

static HANDLE g_map = nullptr;
static void* g_view = nullptr;

extern "C" __declspec(dllexport) bool OpenSharedMemory() {
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(GameSnapshot), L"MecchaGameData");
    if (!g_map && GetLastError() == ERROR_ALREADY_EXISTS)
        g_map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"MecchaGameData");
    if (!g_map) return false;
    g_view = MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(GameSnapshot));
    return g_view != nullptr;
}

extern "C" __declspec(dllexport) void CloseSharedMemory() {
    if (g_view) { UnmapViewOfFile(g_view); g_view = nullptr; }
    if (g_map) { CloseHandle(g_map); g_map = nullptr; }
}

extern "C" __declspec(dllexport) void WriteSnapshot(const GameSnapshot* snap) {
    if (g_view) CopyMemory(g_view, snap, sizeof(GameSnapshot));
}

extern "C" __declspec(dllexport) bool Ping() { return true; }

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) CloseSharedMemory();
    return TRUE;
}
