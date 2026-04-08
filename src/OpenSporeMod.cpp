#include "OpenSporeMod.h"
#include <Spore\Simulator\SimulatorMessages.h>
#include <nlohmann/json.hpp>
#include <shlobj.h>
#include <sstream>
#include <random>
#include <iomanip>

#pragma comment(lib, "shlwapi.lib")

using json = nlohmann::json;

namespace OpenSpore {

static OpenSporeMod* sInstance = nullptr;
OpenSporeMod* GetOpenSporeMod() { return sInstance; }

OpenSporeMod::OpenSporeMod() : mApiClient(mHttpClient) {
    sInstance = this;
}
OpenSporeMod::~OpenSporeMod() {
    if (sInstance == this) sInstance = nullptr;
}

// ============================================================================
// Lifecycle
// ============================================================================

void OpenSporeMod::Initialize() {
    LoadConfig();
    mHttpClient.SetBaseUrl(mServerHost, mServerPort);
    if (!mAuthToken.empty()) mHttpClient.SetBearerToken(mAuthToken);

    mNetworkMenu.SetCallbacks(
        [this](const ConnectParams& p) { OnConnectRequested(p); },
        [this]()                       { OnResetIdentity(); }
    );

    MessageManager.AddUnmanagedListener(this, Simulator::kMsgSwitchGameMode);
    MessageManager.AddUnmanagedListener(this, Simulator::kMsgStarOwnershipChanged);

    // Register per-frame update via lambda
    mUpdateListener = App::AddUpdateFunction([this]() { Update(); });
}

void OpenSporeMod::Dispose() {
    ShutdownUI();
    mSseClient.Disconnect();
    mHttpClient.Shutdown();

    if (mUpdateListener) {
        App::RemoveUpdateFunction(mUpdateListener);
        mUpdateListener = nullptr;
    }

    MessageManager.RemoveListener(this, Simulator::kMsgSwitchGameMode);
    MessageManager.RemoveListener(this, Simulator::kMsgStarOwnershipChanged);
}

// ============================================================================
// IUpdatable::Update
// ============================================================================

void OpenSporeMod::Update() {
    mHttpClient.Update();
    mSseClient.Update();

    if (!mInSpaceStage) return;

    bool f9Down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9Down && !mF9WasDown) {
        if (mNetworkMenu.IsVisible()) mNetworkMenu.Hide();
        else                         mNetworkMenu.Show();
    }
    mF9WasDown = f9Down;

    const float dt = 1.0f / 20.0f;

    if (mRegistered) {
        mHeartbeatTimer += dt;
        if (mHeartbeatTimer >= kHeartbeatInterval) {
            mHeartbeatTimer = 0.0f;
            SendHeartbeat();
        }
    }

    mGalaxyPollTimer += dt;
    if (mGalaxyPollTimer >= kGalaxyPollInterval) {
        mGalaxyPollTimer = 0.0f;
        PollGalaxyEmpires();
    }
}

// ============================================================================
// IUnmanagedMessageListener::HandleMessage
// ============================================================================

bool OpenSporeMod::HandleMessage(uint32_t messageID, void* msg) {
    if (messageID == Simulator::kMsgSwitchGameMode) {
        uint32_t mode = GameModeManager.GetActiveModeID();
        bool wasInSpace = mInSpaceStage;
        mInSpaceStage = (mode == 0x1A0B24E0); // kGameModeSpaceGame

        if ( mInSpaceStage && !wasInSpace) OnSpaceEntered();
        if (!mInSpaceStage &&  wasInSpace) OnSpaceLeft();
    }
    return false;
}

// ============================================================================
// Space enter / leave
// ============================================================================

void OpenSporeMod::OnSpaceEntered() {
    mEmpireName = GetPlayerEmpireName();
    InitUI();

    if (!mAuthToken.empty()) {
        mRegistered = true;
        mHttpClient.SetBearerToken(mAuthToken);
        SendHeartbeat();
    } else {
        RegisterLocalEmpire();
    }

    mSseClient.SetEventCallback([this](const SseEvent& ev) { OnSseEvent(ev); });
    mSseClient.Connect(mServerHost, mServerPort, "/events/stream");
    PollGalaxyEmpires();
    mNetworkMenu.SetDefaultAddress(mServerHost, mServerPort);
}

void OpenSporeMod::OnSpaceLeft() {
    mSseClient.Disconnect();
    mConnected = false;
    ShutdownUI();
    std::lock_guard<std::mutex> lock(mEmpiresMutex);
    GalaxyOverlay::UnmarkAllStars(mRemoteEmpires);
    mRemoteEmpires.clear();
}

// ============================================================================
// UI
// ============================================================================

void OpenSporeMod::InitUI() {
    mGalaxyOverlay.Show();
    RefreshUI();
}

void OpenSporeMod::ShutdownUI() {
    mNetworkMenu.Hide();
    mGalaxyOverlay.Hide();
}

void OpenSporeMod::RefreshUI() {
    std::string statusText;
    if (!mRegistered) {
        statusText = "Connecting to " + mServerHost + ":" + std::to_string(mServerPort) + "...";
    } else if (mConnected) {
        statusText = "Connected  -  " + mServerHost + ":" + std::to_string(mServerPort);
    } else {
        statusText = "Server unreachable: " + mServerHost + ":" + std::to_string(mServerPort);
    }

    mNetworkMenu.SetStatus(statusText, mConnected);

    {
        std::lock_guard<std::mutex> lock(mEmpiresMutex);
        mNetworkMenu.SetPlayers(mRemoteEmpires);
        mGalaxyOverlay.Update(mRemoteEmpires, mConnected, mEmpireName);
    }
}

// ============================================================================
// NetworkMenu callbacks
// ============================================================================

void OpenSporeMod::OnConnectRequested(const ConnectParams& p) {
    mServerHost = p.host;
    mServerPort = p.port;
    SaveConfig();

    mHttpClient.SetBaseUrl(mServerHost, mServerPort);
    mSseClient.Disconnect();
    mRegistered = false;
    mConnected  = false;

    RegisterLocalEmpire();
    mSseClient.SetEventCallback([this](const SseEvent& ev) { OnSseEvent(ev); });
    mSseClient.Connect(mServerHost, mServerPort, "/events/stream");
    mNetworkMenu.SetDefaultAddress(mServerHost, mServerPort);
    RefreshUI();
}

void OpenSporeMod::OnResetIdentity() {
    mPlayerId.clear();
    mEmpireId.clear();
    mAuthToken.clear();
    mRegistered = false;
    mConnected  = false;
    SaveConfig();
    RegisterLocalEmpire();
    RefreshUI();
}

// ============================================================================
// Empire registration
// ============================================================================

void OpenSporeMod::RegisterLocalEmpire() {
    if (mPlayerId.empty()) mPlayerId = GeneratePlayerId();

    mApiClient.RegisterEmpire(
        mPlayerId,
        GetPlayerEmpireName(),
        GetPlayerHomeWorldName(),
        GetPlayerEmpireColor(),
        [this](bool ok, const EmpireData& emp, const std::string& token) {
            OnRegistered(ok, emp, token);
        }
    );
}

void OpenSporeMod::OnRegistered(bool ok, const EmpireData& emp, const std::string& token) {
    if (ok) {
        mEmpireId  = emp.id;
        mAuthToken = token;
        mRegistered = true;
        mConnected  = true;
        mHttpClient.SetBearerToken(mAuthToken);
        SaveConfig();
        SendHeartbeat();
    } else if (!mAuthToken.empty()) {
        // 409 - already registered with this playerId
        mRegistered = true;
        mConnected  = true;
        SendHeartbeat();
    } else {
        mConnected = false;
    }
    RefreshUI();
}

void OpenSporeMod::SendHeartbeat() {
    if (mEmpireId.empty()) return;
    mApiClient.SendHeartbeat(mEmpireId, [this](bool ok) {
        mConnected = ok;
        RefreshUI();
    });
}

// ============================================================================
// Galaxy sync
// ============================================================================

void OpenSporeMod::PollGalaxyEmpires() {
    mApiClient.GetGalaxyEmpires(1, 100, [this](bool ok, const GalaxyResponse& data) {
        OnGalaxyData(ok, data);
    });
}

void OpenSporeMod::OnGalaxyData(bool success, const GalaxyResponse& data) {
    if (!success) return;
    {
        std::lock_guard<std::mutex> lock(mEmpiresMutex);
        GalaxyOverlay::UnmarkAllStars(mRemoteEmpires);
        mRemoteEmpires.clear();
        for (auto& emp : data.empires) {
            if (emp.id != mEmpireId) mRemoteEmpires[emp.id] = emp;
        }
    }
    RefreshUI();
}

// ============================================================================
// SSE events
// ============================================================================

void OpenSporeMod::OnSseEvent(const SseEvent& event) {
    try {
        auto data = json::parse(event.data);
        if      (event.name == "empire:joined")      HandleEmpireJoined(data);
        else if (event.name == "diplomacy:offer")    HandleDiplomacyOffer(data);
        else if (event.name == "diplomacy:accepted") HandleDiplomacyAccepted(data);
        else if (event.name == "diplomacy:rejected") HandleDiplomacyRejected(data);
    } catch (...) {}
}

void OpenSporeMod::HandleEmpireJoined(const json& data) {
    auto emp = EmpireData::FromJson(data);
    if (emp.id == mEmpireId) return;
    {
        std::lock_guard<std::mutex> lock(mEmpiresMutex);
        mRemoteEmpires[emp.id] = emp;
    }
    RefreshUI();
}

void OpenSporeMod::HandleDiplomacyOffer(const json& data) {
    auto offer = DiplomacyOffer::FromJson(data);
    if (offer.toId != mEmpireId) return;
    if (offer.type == "alliance" || offer.type == "trade") {
        mApiClient.AcceptOffer(offer.id, [](bool) {});
    }
}

void OpenSporeMod::HandleDiplomacyAccepted(const json&) {}
void OpenSporeMod::HandleDiplomacyRejected(const json&) {}

// ============================================================================
// Helpers
// ============================================================================

std::string OpenSporeMod::GetPlayerEmpireName() {
    auto* empire = Simulator::GetPlayerEmpire();
    if (empire) {
        auto& n = empire->mEmpireName;
        return std::string(n.begin(), n.end());
    }
    return "Unknown Empire";
}

std::string OpenSporeMod::GetPlayerHomeWorldName() {
    auto* empire = Simulator::GetPlayerEmpire();
    if (empire) {
        auto* star = empire->GetHomeStarRecord();
        if (star) {
            auto& n = star->mName;
            return std::string(n.begin(), n.end());
        }
    }
    return "Unknown Star";
}

std::array<int, 3> OpenSporeMod::GetPlayerEmpireColor() {
    auto* empire = Simulator::GetPlayerEmpire();
    if (empire) {
        auto c = empire->UpdateAndGetColor();
        return { (int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255) };
    }
    return { 128, 128, 255 };
}

std::string OpenSporeMod::GeneratePlayerId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << dist(gen) << "-"
        << std::setw(4) << (dist(gen) & 0xFFFF) << "-"
        << std::setw(4) << (dist(gen) & 0xFFFF) << "-"
        << std::setw(4) << (dist(gen) & 0xFFFF) << "-"
        << std::setw(8) << dist(gen)
        << std::setw(4) << (dist(gen) & 0xFFFF);
    return oss.str();
}

// ============================================================================
// Config
// ============================================================================

std::string OpenSporeMod::GetConfigPath() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        std::string dir = std::string(path) + "\\OpenSpore";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\config.json";
    }
    return "openspore_config.json";
}

void OpenSporeMod::LoadConfig() {
    std::ifstream f(GetConfigPath());
    if (!f.is_open()) return;
    try {
        json c; f >> c;
        mPlayerId   = c.value("playerId",   "");
        mEmpireId   = c.value("empireId",   "");
        mAuthToken  = c.value("authToken",  "");
        mServerHost = c.value("serverHost", "localhost");
        mServerPort = c.value("serverPort", 8080);
    } catch (...) {}
}

void OpenSporeMod::SaveConfig() {
    std::ofstream f(GetConfigPath());
    if (!f.is_open()) return;
    json c;
    c["playerId"]   = mPlayerId;
    c["empireId"]   = mEmpireId;
    c["authToken"]  = mAuthToken;
    c["serverHost"] = mServerHost;
    c["serverPort"] = mServerPort;
    f << c.dump(2);
}

} // namespace OpenSpore
