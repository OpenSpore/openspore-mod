#include "OpenSporeMod.h"
#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\SubSystem\GameNounManager.h>
#include <Spore\App\IMessageManager.h>
#include <Spore\App\cGameModeManager.h>

#include <nlohmann/json.hpp>
#include <shlobj.h>
#include <sstream>
#include <random>
#include <iomanip>

#pragma comment(lib, "shlwapi.lib")

using json = nlohmann::json;

namespace OpenSpore {

static OpenSporeMod* sInstance = nullptr;

OpenSporeMod* GetOpenSporeMod() {
    return sInstance;
}

OpenSporeMod::OpenSporeMod()
    : mApiClient(mHttpClient)
{
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

    if (!mAuthToken.empty()) {
        mHttpClient.SetBearerToken(mAuthToken);
        mRegistered = true;
    }

    // Listen for game mode changes to detect Space stage entry/exit
    App::MessageManager()->AddUnmanagedListener(this, Simulator::kMsgSwitchGameMode);
    App::MessageManager()->AddUnmanagedListener(this, Simulator::kMsgStarOwnershipChanged);

    // Register as updatable for per-frame ticking
    App::AddUpdateFunction(this);
}

void OpenSporeMod::Dispose() {
    mSseClient.Disconnect();
    mHttpClient.Shutdown();
    App::RemoveUpdateFunction(this);
    App::MessageManager()->RemoveListener(this, Simulator::kMsgSwitchGameMode);
    App::MessageManager()->RemoveListener(this, Simulator::kMsgStarOwnershipChanged);
}

// ============================================================================
// IUpdatable
// ============================================================================

void OpenSporeMod::Update() {
    if (!mInSpaceStage) return;

    // Dispatch pending HTTP callbacks and SSE events on the game thread
    mHttpClient.Update();
    mSseClient.Update();

    // Calculate delta time (Spore runs at roughly 15-30 fps in galaxy view)
    // We use a fixed estimate; for precision, use App::GetDeltaTime() if available
    const float dt = 1.0f / 20.0f;

    // Heartbeat
    if (mRegistered) {
        mHeartbeatTimer += dt;
        if (mHeartbeatTimer >= kHeartbeatInterval) {
            mHeartbeatTimer = 0.0f;
            SendHeartbeat();
        }
    }

    // Galaxy polling
    mGalaxyPollTimer += dt;
    if (mGalaxyPollTimer >= kGalaxyPollInterval) {
        mGalaxyPollTimer = 0.0f;
        PollGalaxyEmpires();
    }
}

// ============================================================================
// IUnmanagedMessageListener
// ============================================================================

bool OpenSporeMod::HandleMessage(uint32_t messageID, void* msg) {
    if (messageID == Simulator::kMsgSwitchGameMode) {
        // Check if we entered the Space stage
        // The game mode ID for Space is Simulator::kGameModeSpaceGame (0x1A0B24E0)
        auto* gameModeManager = App::GameModeManager();
        if (gameModeManager) {
            uint32_t currentMode = gameModeManager->GetActiveModeID();
            bool wasInSpace = mInSpaceStage;
            mInSpaceStage = (currentMode == 0x1A0B24E0); // kGameModeSpaceGame

            if (mInSpaceStage && !wasInSpace) {
                // Entered space stage
                if (!mRegistered) {
                    RegisterLocalEmpire();
                } else {
                    // Reconnect - send immediate heartbeat
                    SendHeartbeat();
                }

                // Start SSE event stream
                mSseClient.SetEventCallback([this](const SseEvent& ev) {
                    OnSseEvent(ev);
                });
                mSseClient.Connect(mServerHost, mServerPort, "/events/stream");

                // Initial galaxy fetch
                PollGalaxyEmpires();
            } else if (!mInSpaceStage && wasInSpace) {
                // Left space stage
                mSseClient.Disconnect();
            }
        }
    }

    if (messageID == Simulator::kMsgStarOwnershipChanged && mRegistered) {
        // A star changed ownership - could be the player colonizing
        // Future: sync this event to the server
    }

    return false;
}

// ============================================================================
// Empire registration
// ============================================================================

void OpenSporeMod::RegisterLocalEmpire() {
    if (mPlayerId.empty()) {
        mPlayerId = GeneratePlayerId();
    }

    std::string name = GetPlayerEmpireName();
    std::string homeWorld = GetPlayerHomeWorldName();
    auto color = GetPlayerEmpireColor();

    mApiClient.RegisterEmpire(mPlayerId, name, homeWorld, color,
        [this](bool success, const EmpireData& empire, const std::string& token) {
            OnRegistered(success, empire, token);
        });
}

void OpenSporeMod::OnRegistered(bool success, const EmpireData& empire, const std::string& token) {
    if (!success) {
        // Registration failed (maybe already registered) - retry with heartbeat
        // if we have stored credentials
        if (!mAuthToken.empty()) {
            mRegistered = true;
            SendHeartbeat();
        }
        return;
    }

    mEmpireId = empire.id;
    mAuthToken = token;
    mRegistered = true;
    mHttpClient.SetBearerToken(mAuthToken);
    SaveConfig();

    // Send first heartbeat
    SendHeartbeat();
}

// ============================================================================
// Heartbeat
// ============================================================================

void OpenSporeMod::SendHeartbeat() {
    if (mEmpireId.empty()) return;
    mApiClient.SendHeartbeat(mEmpireId, [](bool success) {
        // Silent - heartbeat failures are not critical
    });
}

// ============================================================================
// Galaxy sync
// ============================================================================

void OpenSporeMod::PollGalaxyEmpires() {
    mApiClient.GetGalaxyEmpires(1, 100, [this](bool success, const GalaxyResponse& data) {
        OnGalaxyData(success, data);
    });
}

void OpenSporeMod::OnGalaxyData(bool success, const GalaxyResponse& data) {
    if (!success) return;

    std::lock_guard<std::mutex> lock(mEmpiresMutex);
    mRemoteEmpires.clear();
    for (auto& empire : data.empires) {
        if (empire.id != mEmpireId) { // Don't include self
            mRemoteEmpires[empire.id] = empire;
        }
    }

    UpdateGalaxyOverlay();
}

// ============================================================================
// SSE events
// ============================================================================

void OpenSporeMod::OnSseEvent(const SseEvent& event) {
    try {
        auto data = json::parse(event.data);

        if (event.name == "empire:joined") {
            HandleEmpireJoined(data);
        } else if (event.name == "diplomacy:offer") {
            HandleDiplomacyOffer(data);
        } else if (event.name == "diplomacy:accepted") {
            HandleDiplomacyAccepted(data);
        } else if (event.name == "diplomacy:rejected") {
            HandleDiplomacyRejected(data);
        }
    } catch (...) {
        // Malformed event, ignore
    }
}

void OpenSporeMod::HandleEmpireJoined(const json& data) {
    auto empire = EmpireData::FromJson(data);
    if (empire.id == mEmpireId) return;

    {
        std::lock_guard<std::mutex> lock(mEmpiresMutex);
        mRemoteEmpires[empire.id] = empire;
    }

    UpdateGalaxyOverlay();

    // TODO: Show in-game notification "Empire X has joined the galaxy!"
}

void OpenSporeMod::HandleDiplomacyOffer(const json& data) {
    auto offer = DiplomacyOffer::FromJson(data);
    if (offer.toId != mEmpireId) return;

    // TODO: Show in-game UI for incoming diplomacy offer
    // For now, auto-accept alliances and trade
    if (offer.type == "alliance" || offer.type == "trade") {
        mApiClient.AcceptOffer(offer.id, [](bool) {});
    }
}

void OpenSporeMod::HandleDiplomacyAccepted(const json& data) {
    // TODO: Show notification that offer was accepted
}

void OpenSporeMod::HandleDiplomacyRejected(const json& data) {
    // TODO: Show notification that offer was rejected
}

// ============================================================================
// Galaxy overlay - render remote empires on the galaxy map
// ============================================================================

void OpenSporeMod::UpdateGalaxyOverlay() {
    // Access the star manager to find stars matching remote empires' homeworlds
    auto* starMgr = Simulator::StarManager();
    if (!starMgr) return;

    std::lock_guard<std::mutex> lock(mEmpiresMutex);

    for (auto& [id, empire] : mRemoteEmpires) {
        // The homeWorld field contains the star/planet name from the server.
        // In a full implementation, we would:
        // 1. Map homeWorld names to star IDs or positions
        // 2. Create visual markers (custom models or colored rings)
        // 3. Add tooltip/hover info showing the player's empire name
        //
        // For now, this is a placeholder for the overlay system.
        // The actual rendering requires hooking into Spore's galaxy view renderer
        // or using UTFWin to draw overlay UI elements.
    }
}

// ============================================================================
// Helpers
// ============================================================================

std::string OpenSporeMod::GetPlayerEmpireName() {
    auto* playerData = Simulator::SpacePlayerData();
    if (playerData) {
        auto* empire = Simulator::GetPlayerEmpire();
        if (empire) {
            // Convert string16 to std::string
            auto& name16 = empire->GetName();
            std::string result(name16.begin(), name16.end());
            return result;
        }
    }
    return "Unknown Empire";
}

std::string OpenSporeMod::GetPlayerHomeWorldName() {
    auto* empire = Simulator::GetPlayerEmpire();
    if (empire) {
        auto* homeStar = empire->GetHomeStarRecord();
        if (homeStar) {
            auto& name16 = homeStar->GetName();
            std::string result(name16.begin(), name16.end());
            return result;
        }
    }
    return "Unknown Star";
}

std::array<int, 3> OpenSporeMod::GetPlayerEmpireColor() {
    auto* empire = Simulator::GetPlayerEmpire();
    if (empire) {
        auto color = empire->UpdateAndGetColor();
        return {
            (int)(color.r * 255.0f),
            (int)(color.g * 255.0f),
            (int)(color.b * 255.0f)
        };
    }
    return {128, 128, 255}; // default blue
}

std::string OpenSporeMod::GeneratePlayerId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8) << dist(gen) << "-";
    oss << std::setw(4) << (dist(gen) & 0xFFFF) << "-";
    oss << std::setw(4) << (dist(gen) & 0xFFFF) << "-";
    oss << std::setw(4) << (dist(gen) & 0xFFFF) << "-";
    oss << std::setw(8) << dist(gen) << std::setw(4) << (dist(gen) & 0xFFFF);
    return oss.str();
}

// ============================================================================
// Config persistence - saves token/empire ID to AppData
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
    std::string configPath = GetConfigPath();
    std::ifstream file(configPath);
    if (!file.is_open()) return;

    try {
        json config;
        file >> config;
        mPlayerId = config.value("playerId", "");
        mEmpireId = config.value("empireId", "");
        mAuthToken = config.value("authToken", "");
        mServerHost = config.value("serverHost", "localhost");
        mServerPort = config.value("serverPort", 8080);
    } catch (...) {
        // Corrupted config, start fresh
    }
}

void OpenSporeMod::SaveConfig() {
    std::string configPath = GetConfigPath();
    std::ofstream file(configPath);
    if (!file.is_open()) return;

    json config;
    config["playerId"] = mPlayerId;
    config["empireId"] = mEmpireId;
    config["authToken"] = mAuthToken;
    config["serverHost"] = mServerHost;
    config["serverPort"] = mServerPort;

    file << config.dump(2);
}

} // namespace OpenSpore
