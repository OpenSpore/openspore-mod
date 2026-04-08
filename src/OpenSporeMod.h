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
#include "ui/NetworkMenu.h"
#include "ui/GalaxyOverlay.h"

#include <string>
#include <unordered_map>
#include <fstream>
#include <mutex>

namespace OpenSpore {

/// Main mod class: network sync + UI for Space Stage multiplayer.
class OpenSporeMod
    : public App::IUpdatable
    , public App::IUnmanagedMessageListener
    , public DefaultRefCounted
{
public:
    static const uint32_t TYPE = id("OpenSporeMod");

    OpenSporeMod();
    ~OpenSporeMod();

    void Initialize();
    void Dispose();

    // App::IUpdatable
    void Update() override;

    // App::IUnmanagedMessageListener
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

    // -- UI --
    void InitUI();
    void ShutdownUI();
    void OnSpaceEntered();
    void OnSpaceLeft();
    void RefreshUI();         // push current state to menu + overlay

    // -- NetworkMenu callbacks --
    void OnConnectRequested(const ConnectParams& p);
    void OnResetIdentity();

    // -- Helpers --
    std::string GetPlayerEmpireName();
    std::string GetPlayerHomeWorldName();
    std::array<int, 3> GetPlayerEmpireColor();
    std::string GeneratePlayerId();

    // -- Network --
    HttpClient mHttpClient;
    ApiClient  mApiClient;
    SseClient  mSseClient;

    // -- UI --
    NetworkMenu   mNetworkMenu;
    GalaxyOverlay mGalaxyOverlay;

    // -- State --
    std::string mPlayerId;
    std::string mEmpireName; // cached
    std::string mEmpireId;
    std::string mAuthToken;
    bool mRegistered    = false;
    bool mInSpaceStage  = false;
    bool mConnected     = false; // server reachable + heartbeat OK

    // -- Key toggle (F9) --
    bool mF9WasDown = false;

    // -- Timers --
    float mHeartbeatTimer   = 0.0f;
    float mGalaxyPollTimer  = 0.0f;
    static constexpr float kHeartbeatInterval  = 30.0f;
    static constexpr float kGalaxyPollInterval = 15.0f;

    // -- Remote empires --
    std::mutex mEmpiresMutex;
    std::unordered_map<std::string, EmpireData> mRemoteEmpires;

    // -- Config --
    std::string mServerHost = "localhost";
    int mServerPort = 8080;
};

OpenSporeMod* GetOpenSporeMod();

} // namespace OpenSpore
