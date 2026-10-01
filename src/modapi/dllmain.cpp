// dllmain.cpp - OpenSporeMP: Spore ModAPI mod (Galactic Adventures, Steam/GOG/EA 3.1.0.x).
//
// Console cheats (open the console with Ctrl+Shift+C):
//   mpHost [port]            host a 2-player session on a UDP port (default 7777)
//   mpJoin <ip> [port]       join a host (dotted-quad IP)
//   mpLeave                  leave the session
//   mpStatus                 print session / replication status
//   mpGhostMode <mode>       moveto | teleport | setpos | velocity | raw
//   mpGhostTest / mpGhostKill   spawn / remove a local test ghost circling your ship (no network)
//   mpGhosts on|off          enable / disable remote ghosts
//   mpOpt <name> <value>     neutralise | clearbehaviors | spawnufo | movetoevery | ownpol
//   mpDump                   describe the player ship
//
// Two instances on one PC: start the second game with the environment variable
// OSMP_PROFILE=2 (separate AppData/My Spore Creations folders + renamed single-instance mutex,
// technique from SporeCoop) and host from one window, join 127.0.0.1 from the other.
#include "stdafx.h"
#include "ModApiEngine.h"
#include "game/GhostLab.h"
#include "game/Replication.h"
#include "net/Session.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

using namespace osmp;

namespace {

FILE* g_logFile = nullptr;

void LogLine(const char* line) {
    ModAPI::Log("[OpenSporeMP] %s", line);
    if (g_logFile) {
        SYSTEMTIME st; GetLocalTime(&st);
        std::fprintf(g_logFile, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line);
        std::fflush(g_logFile);
    }
}

void Logf(const char* fmt, ...) {
    char buf[512];
    va_list a; va_start(a, fmt); std::vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
    LogLine(buf);
}

std::unique_ptr<ModApiEngine> g_engine;
std::unique_ptr<Session> g_session;
std::unique_ptr<Replication> g_replication;
std::unique_ptr<GhostLab> g_ghostLab;
UpdateMessageListenerPtr g_updateListener;
bool g_inTick = false;

void Tick() {
    if (g_inTick || !g_engine) return;
    g_inTick = true;
    g_session->update(g_engine->nowMs());
    g_replication->update();
    g_ghostLab->update();
    g_inTick = false;
}

// A cheat whose behaviour is a lambda, so every mp* command is three lines.
class LambdaCheat : public ArgScript::ICommand {
public:
    using Fn = std::function<void(const ArgScript::Line&, ArgScript::FormatParser*)>;
    LambdaCheat(const char* basic, const char* full, Fn fn) : basic_(basic), full_(full), fn_(std::move(fn)) {}
    void ParseLine(const ArgScript::Line& line) override { fn_(line, mpFormatParser); }
    const char* GetDescription(ArgScript::DescriptionMode mode) const override {
        return mode == ArgScript::DescriptionMode::Basic ? basic_ : full_;
    }
private:
    const char* basic_;
    const char* full_;
    Fn fn_;
};

void Print(const std::string& s) { App::ConsolePrintF("%s", s.c_str()); LogLine(s.c_str()); }

void AddCheats() {
    CheatManager.AddCheat("mpHost", new LambdaCheat("Host an OpenSpore multiplayer session.", "mpHost [port]\nHost a 2-player session on a UDP port (default 7777).",
        [](const ArgScript::Line& line, ArgScript::FormatParser* fp) {
            size_t n; auto args = line.GetArgumentsRange(&n, 0, 1);
            uint16_t port = n >= 1 ? (uint16_t)fp->ParseInt(args[0]) : 7777;
            if (g_session->host(port)) Print("mp: hosting on udp port " + std::to_string(g_session->localPort()));
            else Print("mp: host failed: " + g_session->lastError());
        }));
    CheatManager.AddCheat("mpJoin", new LambdaCheat("Join an OpenSpore multiplayer session.", "mpJoin <ip> [port]\nJoin a host by IPv4 address (default port 7777).",
        [](const ArgScript::Line& line, ArgScript::FormatParser* fp) {
            size_t n; auto args = line.GetArgumentsRange(&n, 1, 2);
            uint16_t port = n >= 2 ? (uint16_t)fp->ParseInt(args[1]) : 7777;
            Endpoint ep;
            if (!Endpoint::parse(args[0], port, ep)) { Print("mp: bad ip (dotted quad only)"); return; }
            if (g_session->join(ep)) Print("mp: joining " + ep.toString());
            else Print("mp: join failed: " + g_session->lastError());
        }));
    CheatManager.AddCheat("mpLeave", new LambdaCheat("Leave the multiplayer session.", "mpLeave\nLeave the current session and remove ghosts.",
        [](const ArgScript::Line&, ArgScript::FormatParser*) { g_session->leave("cheat"); g_replication->onDisconnected(); Print("mp: left"); }));
    CheatManager.AddCheat("mpStatus", new LambdaCheat("Show multiplayer status.", "mpStatus\nSession, replication and ghost lab status.",
        [](const ArgScript::Line&, ArgScript::FormatParser*) {
            Frame f; bool hf = g_engine->getFrame(f);
            char b[160]; std::snprintf(b, sizeof(b), "mp: inSpace=%d frame=%s star=%08X planet=%08X", g_engine->isInSpaceStage() ? 1 : 0, hf ? frameKindName(f.kind) : "?", f.starId, f.planetId);
            Print(b); Print(g_replication->statusLine()); Print(g_ghostLab->statusLine());
        }));
    CheatManager.AddCheat("mpGhostMode", new LambdaCheat("Set how remote ghosts are moved.", "mpGhostMode moveto|teleport|setpos|velocity|raw",
        [](const ArgScript::Line& line, ArgScript::FormatParser*) {
            size_t n; auto args = line.GetArgumentsRange(&n, 1, 1);
            GhostDriveMode m;
            if (!parseGhostDriveMode(args[0], m)) { Print("mp: unknown mode"); return; }
            g_replication->setDriveMode(m); g_ghostLab->setMode(m);
            Print(std::string("mp: ghost drive mode = ") + ghostDriveModeName(m));
        }));
    CheatManager.AddCheat("mpGhostTest", new LambdaCheat("Spawn a local test ghost (no network).", "mpGhostTest\nSpawns a ghost UFO that circles your ship using the current drive mode. Watch mpStatus / the log for the position error.",
        [](const ArgScript::Line&, ArgScript::FormatParser*) { if (!g_ghostLab->start(g_replication->driveMode())) Print("mp: ghost test failed (see log)"); else Print("mp: test ghost spawned"); }));
    CheatManager.AddCheat("mpGhostKill", new LambdaCheat("Remove the local test ghost.", "mpGhostKill",
        [](const ArgScript::Line&, ArgScript::FormatParser*) { g_ghostLab->stop(); Print("mp: test ghost removed"); }));
    CheatManager.AddCheat("mpGhosts", new LambdaCheat("Enable or disable remote ghosts.", "mpGhosts on|off",
        [](const ArgScript::Line& line, ArgScript::FormatParser*) {
            size_t n; auto args = line.GetArgumentsRange(&n, 1, 1);
            bool on = std::string(args[0]) != "off";
            g_replication->setGhostsEnabled(on); Print(on ? "mp: ghosts on" : "mp: ghosts off");
        }));
    CheatManager.AddCheat("mpOpt", new LambdaCheat("Toggle experimental ghost options.", "mpOpt <neutralise|clearbehaviors|spawnufo|ownpol|movetoevery> <value>",
        [](const ArgScript::Line& line, ArgScript::FormatParser* fp) {
            size_t n; auto args = line.GetArgumentsRange(&n, 2, 2);
            std::string name = args[0]; int v = fp->ParseInt(args[1]);
            ModApiEngine::Options& o = g_engine->options();
            if (name == "neutralise") o.neutraliseAI = v != 0;
            else if (name == "clearbehaviors") o.clearBehaviors = v != 0;
            else if (name == "spawnufo") o.useSpawnUFO = v != 0;
            else if (name == "movetoevery") o.moveToEveryTicks = v > 0 ? v : 1;
            else if (name == "ownpol") { Replication::Config c = g_replication->config(); (void)c; Print("mp: ownpol is fixed in this build (use the lab DLL to test the remote political id)"); return; }
            else { Print("mp: unknown option"); return; }
            Print("mp: " + name + " = " + std::to_string(v));
        }));
    CheatManager.AddCheat("mpDump", new LambdaCheat("Describe the player ship.", "mpDump",
        [](const ArgScript::Line&, ArgScript::FormatParser*) {
            Simulator::cGameDataUFO* ufo = Simulator::GetPlayerUFO();
            if (!ufo) { Print("mp: no player ship"); return; }
            g_engine->describe(ufo, "player");
            Print("mp: player ship described in the log");
        }));
}

// ---- two instances on one PC (SporeCoop technique) --------------------------------------

bool IsProfile2() {
    char v[8] = {};
    return GetEnvironmentVariableA("OSMP_PROFILE", v, sizeof(v)) > 0 && v[0] == '2';
}

typedef HANDLE(WINAPI* CreateMutexA_t)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
CreateMutexA_t g_createMutexA = nullptr;

HANDLE WINAPI CreateMutexAHook(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCSTR name) {
    if (name && name[0] && IsProfile2()) {
        char isolated[512];
        std::snprintf(isolated, sizeof(isolated), "%s_OSMP2", name);
        return g_createMutexA(attributes, initialOwner, isolated);
    }
    return g_createMutexA(attributes, initialOwner, name);
}

member_detour(SetUserDirNames_detour, App::cAppSystem, void(const char16_t*, const char16_t*)) {
    void detoured(const char16_t* creationsFolderName, const char16_t* appDataFolderName) {
        if (IsProfile2()) {
            LogLine("profile 2: using 'My Spore Creations OSMP2' / 'SporeOSMP2'");
            original_function(this, u"My Spore Creations OSMP2", u"SporeOSMP2");
            return;
        }
        original_function(this, creationsFolderName, appDataFolderName);
    }
};

} // namespace

void Initialize() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char* slash = std::strrchr(path, '\\')) slash[1] = 0;
    std::string logPath = std::string(path) + "OpenSporeMP.log";
    g_logFile = std::fopen(logPath.c_str(), "a");
    LogLine("=== OpenSporeMP initialize ===");

    g_engine.reset(new ModApiEngine(LogLine));

    Session::Config sc;
    sc.gameBuild = ModAPI::GetMajorVersion() * 1000000 + ModAPI::GetMinorVersion() * 1000 + ModAPI::GetBuildVersion();
    sc.snapshotHz = 15;
    char user[64] = {}; DWORD ulen = sizeof(user);
    GetUserNameA(user, &ulen);
    sc.playerName = user;
    g_session.reset(new Session(sc));
    g_session->setLogger([](const std::string& l) { Logf("session: %s", l.c_str()); });

    Replication::Config rc;
    g_replication.reset(new Replication(*g_engine, *g_session, rc));
    g_session->setCallbacks(
        [](const Session::PeerInfo& p) { Logf("peer connected: player %u '%s' %s", (unsigned)p.playerId, p.name.c_str(), p.endpoint.toString().c_str()); App::ConsolePrintF("mp: peer connected: %s", p.name.c_str()); g_replication->onConnected(); },
        [](const std::string& r) { Logf("peer disconnected: %s", r.c_str()); App::ConsolePrintF("mp: disconnected: %s", r.c_str()); g_replication->onDisconnected(); },
        [](MsgType t, ByteReader& r) { g_replication->onMessage(t, r); });
    g_ghostLab.reset(new GhostLab(*g_engine));

    AddCheats();
    g_updateListener = App::AddUpdateFunction(Tick);
    LogLine("ready: mpHost / mpJoin <ip> / mpGhostTest / mpStatus");
}

void Dispose() {
    if (g_updateListener) { App::RemoveUpdateFunction(g_updateListener); g_updateListener = nullptr; }
    if (g_session) g_session->leave("dispose");
    g_ghostLab.reset(); g_replication.reset(); g_session.reset(); g_engine.reset();
    if (g_logFile) { std::fclose(g_logFile); g_logFile = nullptr; }
}

void AttachDetours() {
    SetUserDirNames_detour::attach(GetAddress(App::cAppSystem, SetUserDirNames));
    g_createMutexA = (CreateMutexA_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateMutexA");
    if (g_createMutexA) DetourAttach(&(PVOID&)g_createMutexA, (PVOID)CreateMutexAHook);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        ModAPI::AddPostInitFunction(Initialize);
        ModAPI::AddDisposeFunction(Dispose);
        PrepareDetours(hModule);
        AttachDetours();
        CommitDetours();
        break;
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
