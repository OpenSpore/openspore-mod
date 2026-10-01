// LabMain.cpp - osmp_lab.dll: experimental multiplayer DLL for Spore GA (Steam/GOG 3.1.0.29).
//
// No ModAPI dependency: raw addresses + a vtable hook on cSimulatorSpaceGame::Update, the
// same technique as idapro-reverse-dump/src/m0. Everything runs on the simulation thread.
//
// Control is by marker files in the DLL's folder (the m0 workflow):
//   osmp_host.txt       "7777"                 host a session on that UDP port
//   osmp_join.txt       "192.168.1.10 7777"    join a host
//   osmp_leave          (empty)                leave the session
//   osmp_mode.txt       moveto|teleport|setpos|velocity|raw   ghost drive mode
//   osmp_ghosttest      (empty)                spawn a local test ghost circling the player (no network)
//   osmp_ghostkill      (empty)                remove the test ghost
//   osmp_ghosts_off / osmp_ghosts_on           toggle remote ghosts
// Output:
//   osmp_lab.log        log
//   osmp_status.txt     one-line status, rewritten every 2 s
#include "RawAddresses.h"
#include "RawEngine.h"
#include "game/GhostLab.h"
#include "game/Replication.h"
#include "net/Session.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace osmp;

namespace {

HMODULE g_module = nullptr;
std::string g_dir;
FILE* g_log = nullptr;
CRITICAL_SECTION g_logLock;
uintptr_t g_base = 0;

std::unique_ptr<RawEngine> g_engine;
std::unique_ptr<Session> g_session;
std::unique_ptr<Replication> g_replication;
std::unique_ptr<GhostLab> g_ghostLab;

typedef int (__thiscall* UpdateFn)(void* self, unsigned lo, unsigned hi);
UpdateFn g_origUpdate = nullptr;
volatile LONG g_frames = 0;
bool g_inTick = false;

void Log(const std::string& line) {
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME st; GetLocalTime(&st);
    std::fprintf(g_log, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line.c_str());
    std::fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}

void Logf(const char* fmt, ...) {
    char buf[512];
    va_list a; va_start(a, fmt); std::vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
    Log(buf);
}

std::string Path(const char* name) { return g_dir + name; }

bool Consume(const char* name, std::string* content) {
    std::string p = Path(name);
    if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    if (content) {
        content->clear();
        FILE* f = std::fopen(p.c_str(), "rb");
        if (f) { char buf[256]; size_t n = std::fread(buf, 1, sizeof(buf) - 1, f); buf[n] = 0; *content = buf; std::fclose(f); }
        while (!content->empty() && (content->back() == '\n' || content->back() == '\r' || content->back() == ' ')) content->pop_back();
    }
    DeleteFileA(p.c_str());
    return true;
}

void WriteStatus() {
    FILE* f = std::fopen(Path("osmp_status.txt").c_str(), "w");
    if (!f) return;
    Frame fr; bool hasFrame = g_engine->getFrame(fr);
    std::fprintf(f, "frames=%ld inSpace=%d frame=%s star=%08X planet=%08X\n%s\n%s\n",
                 g_frames, g_engine->isInSpaceStage() ? 1 : 0, hasFrame ? frameKindName(fr.kind) : "?", fr.starId, fr.planetId,
                 g_replication->statusLine().c_str(), g_ghostLab->statusLine().c_str());
    std::fclose(f);
}

void PollControlFiles() {
    std::string s;
    if (Consume("osmp_host.txt", &s)) {
        uint16_t port = (uint16_t)std::atoi(s.c_str());
        if (!port) port = 7777;
        if (g_session->host(port)) Logf("control: hosting on udp %u", g_session->localPort());
        else Log("control: host failed: " + g_session->lastError());
    }
    if (Consume("osmp_join.txt", &s)) {
        char ip[64] = {}; unsigned port = 7777;
        if (std::sscanf(s.c_str(), "%63s %u", ip, &port) >= 1) {
            Endpoint ep;
            if (Endpoint::parse(ip, (uint16_t)port, ep)) {
                if (g_session->join(ep)) Logf("control: joining %s", ep.toString().c_str());
                else Log("control: join failed: " + g_session->lastError());
            } else Logf("control: bad ip '%s' (dotted quad only)", ip);
        }
    }
    if (Consume("osmp_leave", nullptr)) { g_session->leave("control"); g_replication->onDisconnected(); Log("control: left"); }
    if (Consume("osmp_mode.txt", &s)) {
        GhostDriveMode m;
        if (parseGhostDriveMode(s, m)) { g_replication->setDriveMode(m); g_ghostLab->setMode(m); Logf("control: drive mode = %s", ghostDriveModeName(m)); }
        else Logf("control: unknown mode '%s'", s.c_str());
    }
    if (Consume("osmp_ghosttest", nullptr)) g_ghostLab->start(g_replication->driveMode());
    if (Consume("osmp_ghostkill", nullptr)) { g_ghostLab->stop(); Log("control: test ghost removed"); }
    if (Consume("osmp_ghosts_off", nullptr)) { g_replication->setGhostsEnabled(false); Log("control: ghosts off"); }
    if (Consume("osmp_ghosts_on", nullptr)) { g_replication->setGhostsEnabled(true); Log("control: ghosts on"); }
    if (Consume("osmp_dump", nullptr)) {
        uintptr_t ship = g_engine->playerShip();
        if (ship) g_engine->describeObject(ship, "player");
        Logf("dump: spaceGame=%08X playerShip=%08X", (unsigned)g_engine->spaceGame(), (unsigned)ship);
    }
}

void LabTick() {
    if (g_inTick) return;   // re-entrancy guard
    g_inTick = true;
    LONG n = InterlockedIncrement(&g_frames);
    if ((n % 15) == 0) PollControlFiles();
    uint32_t now = g_engine->nowMs();
    g_session->update(now);
    g_replication->update();
    g_ghostLab->update();
    if ((n % 60) == 0) WriteStatus();
    g_inTick = false;
}

int __fastcall UpdateHook(void* self, void* /*edx*/, unsigned lo, unsigned hi) {
    int result = g_origUpdate(self, lo, hi);
    LabTick();
    return result;
}

bool InstallTickHook() {
    uintptr_t vt = g_engine->rt(raw::kSpaceGameVTable);
    uintptr_t slot = vt + raw::kSpaceGameUpdateSlot * sizeof(void*);
    uintptr_t current = 0;
    if (!g_engine->read(slot, current)) { Logf("hook: cannot read vtable slot at %08X", (unsigned)slot); return false; }
    DWORD old = 0;
    if (!VirtualProtect((void*)slot, sizeof(void*), PAGE_READWRITE, &old)) { Logf("hook: VirtualProtect failed %lu", GetLastError()); return false; }
    g_origUpdate = (UpdateFn)current;
    *(uintptr_t*)slot = (uintptr_t)&UpdateHook;
    VirtualProtect((void*)slot, sizeof(void*), old, &old);
    Logf("hook: cSimulatorSpaceGame::Update slot %d: %08X -> %08X", raw::kSpaceGameUpdateSlot, (unsigned)current, (unsigned)(uintptr_t)&UpdateHook);
    return true;
}

DWORD WINAPI Boot(LPVOID) {
    InitializeCriticalSection(&g_logLock);
    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_module, path, MAX_PATH);
    if (char* slash = std::strrchr(path, '\\')) slash[1] = 0;
    g_dir = path;
    g_log = std::fopen(Path("osmp_lab.log").c_str(), "a");
    Log("=== osmp_lab start ===");

    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    Logf("module base = %08X (preferred 00400000, delta %08X)", (unsigned)g_base, (unsigned)(g_base - 0x400000));

    g_engine.reset(new RawEngine(g_base, Log));
    if (!g_engine->selfTest()) { Log("address self-test FAILED - wrong game build? refusing to hook"); return 0; }

    Session::Config sc;
    sc.gameBuild = 3010029;
    sc.snapshotHz = 15;
    char user[64] = {}; DWORD ulen = sizeof(user);
    GetUserNameA(user, &ulen);
    sc.playerName = user;
    g_session.reset(new Session(sc));
    g_session->setLogger([](const std::string& l) { Log("session: " + l); });

    Replication::Config rc;
    g_replication.reset(new Replication(*g_engine, *g_session, rc));
    g_session->setCallbacks([](const Session::PeerInfo& p) { Logf("peer connected: player %u '%s' %s", (unsigned)p.playerId, p.name.c_str(), p.endpoint.toString().c_str()); g_replication->onConnected(); },
                            [](const std::string& r) { Log("peer disconnected: " + r); g_replication->onDisconnected(); },
                            [](MsgType t, ByteReader& r) { g_replication->onMessage(t, r); });
    g_ghostLab.reset(new GhostLab(*g_engine));

    if (!InstallTickHook()) return 0;
    Log("ready: enter the space stage, then drop control files next to the DLL (see LabMain.cpp header)");
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, Boot, nullptr, 0, nullptr);
    }
    return TRUE;
}
