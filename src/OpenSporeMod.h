#pragma once

#include <Spore\BasicIncludes.h>
#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\SubSystem\GameNounManager.h>
#include <Spore\Simulator\SubSystem\SpacePlayerData.h>
#include <Spore\Simulator\cEmpire.h>
#include <Spore\Simulator\cStarRecord.h>
#include <Spore\App\IMessageManager.h>

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
/// Uses lambda-based AddUpdateFunction; implements IUnmanagedMessageListener directly.
class OpenSporeMod
    : public App::IUnmanagedMessageListener
    , public DefaultObject
{
public:
    OpenSporeMod();
    ~OpenSporeMod();

    void Initialize();
    void Dispose();

    // Called every frame via lambda registered with App::AddUpdateFunction
    void Update();

    // App::IUnmanagedMessageListener
    virtual bool HandleMessage(uint32_t messageID, void* msg) override;

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
    void RefreshUI();

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

    // -- Update listener handle --
    eastl::intrusive_ptr<App::UpdateMessageListener> mUpdateListener;

    // -- State --
    std::string mPlayerId;
    std::string mEmpireName;
    std::string mEmpireId;
    std::string mAuthToken;
    bool mRegistered   = false;
    bool mInSpaceStage = false;
    bool mConnected    = false;

    // -- F9 key toggle --
    bool mF9WasDown = false;

    // -- Timers --
    float mHeartbeatTimer  = 0.0f;
    float mGalaxyPollTimer = 0.0f;
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
