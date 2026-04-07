#pragma once

#include <Spore\BasicIncludes.h>
#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\SubSystem\GameNounManager.h>
#include <Spore\Simulator\cEmpire.h>
#include <Spore\Simulator\cStarRecord.h>
#include <Spore\Simulator\cPlanetRecord.h>
#include <Spore\Simulator\SubSystem\SpacePlayerData.h>

#include "net/HttpClient.h"
#include "net/ApiClient.h"
#include "net/SseClient.h"

#include <string>
#include <unordered_map>
#include <fstream>
#include <mutex>

namespace OpenSpore {

/// Main mod class: handles network synchronization for the Space stage.
/// Implements IUpdatable for per-frame updates and IUnmanagedMessageListener for game events.
class OpenSporeMod
    : public App::IUpdatable
    , public App::IUnmanagedMessageListener
    , public DefaultRefCounted
{
public:
    static const uint32_t TYPE = id("OpenSporeMod");

    OpenSporeMod();
    ~OpenSporeMod();

    // -- Lifecycle --
    void Initialize();
    void Dispose();

    // -- IUpdatable --
    void Update() override;

    // -- IUnmanagedMessageListener --
    bool HandleMessage(uint32_t messageID, void* msg) override;

private:
    // -- Persistence --
    void LoadConfig();
    void SaveConfig();
    std::string GetConfigPath();

    // -- Empire registration --
    void RegisterLocalEmpire();
    void OnRegistered(bool success, const EmpireData& empire, const std::string& token);

    // -- Heartbeat --
    void SendHeartbeat();

    // -- Galaxy sync --
    void PollGalaxyEmpires();
    void OnGalaxyData(bool success, const GalaxyResponse& data);

    // -- SSE events --
    void OnSseEvent(const SseEvent& event);
    void HandleEmpireJoined(const nlohmann::json& data);
    void HandleDiplomacyOffer(const nlohmann::json& data);
    void HandleDiplomacyAccepted(const nlohmann::json& data);
    void HandleDiplomacyRejected(const nlohmann::json& data);

    // -- Galaxy overlay --
    void UpdateGalaxyOverlay();

    // -- Helpers --
    std::string GetPlayerEmpireName();
    std::string GetPlayerHomeWorldName();
    std::array<int, 3> GetPlayerEmpireColor();
    std::string GeneratePlayerId();

    // -- Network --
    HttpClient mHttpClient;
    ApiClient mApiClient;
    SseClient mSseClient;

    // -- State --
    std::string mPlayerId;
    std::string mEmpireId;
    std::string mAuthToken;
    bool mRegistered = false;
    bool mInSpaceStage = false;

    // -- Timers --
    float mHeartbeatTimer = 0.0f;
    float mGalaxyPollTimer = 0.0f;
    static constexpr float kHeartbeatInterval = 30.0f;  // seconds
    static constexpr float kGalaxyPollInterval = 15.0f;  // seconds

    // -- Remote empires --
    std::mutex mEmpiresMutex;
    std::unordered_map<std::string, EmpireData> mRemoteEmpires;

    // -- Config --
    std::string mServerHost = "localhost";
    int mServerPort = 8080;
};

/// Global singleton accessor
OpenSporeMod* GetOpenSporeMod();

} // namespace OpenSpore
