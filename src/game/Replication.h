// Replication.h - M3/M4: owner-authoritative replication of player ships as ghosts.
//
// Each player owns its ship: it samples the local pose every tick, sends an EntitySpawn
// once and Transform snapshots at snapshotHz. For the remote ship it keeps a NetEntity
// and, while both players share the same reference frame, a ghost object in the engine
// driven through IEngine::driveGhost at (now - renderDelay) using the interpolation buffer.
//
// Host authority over world state (NPCs, planets, galaxy) is M6+; nothing here assumes it.
#pragma once

#include "EngineFacade.h"
#include "net/EntityRegistry.h"
#include "net/Session.h"

#include <string>

namespace osmp {

class Replication {
public:
    struct Config {
        uint32_t renderDelayMs = 0;        // 0 = 2 snapshot intervals
        GhostDriveMode driveMode = GhostDriveMode::MoveTo;
        bool ghostsEnabled = true;
        bool useOwnPoliticalId = true;     // spawn ghosts under the local empire (safe) instead of the remote one
        uint32_t ghostAliveCheckMs = 1000;
        uint32_t frameResendMs = 2000;     // periodic FrameChange so a lost one self-heals
    };

    struct Stats {
        uint32_t snapshotsSent = 0, snapshotsRecv = 0;
        uint32_t spawnsSent = 0, spawnsRecv = 0;
        uint32_t ghostsSpawned = 0, ghostsDestroyed = 0, ghostSpawnFailures = 0;
        uint32_t frameChangesSent = 0, frameChangesRecv = 0;
        uint32_t ghostsLostByEngine = 0;
    };

    Replication(IEngine& engine, Session& session, const Config& cfg);

    // Wire these to the Session callbacks.
    void onConnected();
    void onDisconnected();
    void onMessage(MsgType type, ByteReader& payload);

    // Call once per simulation tick while the session is active.
    void update();

    void setDriveMode(GhostDriveMode m) { cfg_.driveMode = m; }
    GhostDriveMode driveMode() const { return cfg_.driveMode; }
    void setGhostsEnabled(bool on);
    const Config& config() const { return cfg_; }
    const Stats& stats() const { return stats_; }
    const Frame& localFrame() const { return localFrame_; }
    uint32_t localNetId() const { return localNetId_; }
    const EntityRegistry& registry() const { return reg_; }
    bool remoteGhostVisible() const;
    std::string statusLine() const;

private:
    void sampleAndSendLocal(uint32_t nowMs);
    void handleSpawn(ByteReader& r);
    void handleDespawn(ByteReader& r);
    void handleFrameChange(ByteReader& r);
    void handleTransform(ByteReader& r);
    void reconcileGhosts(uint32_t nowMs);
    void driveGhosts(uint32_t nowMs);
    void destroyGhost(NetEntity& e);
    void sendLocalSpawn(const LocalShipInfo& info, const Pose& pose);
    uint32_t renderDelayMs() const;

    IEngine& engine_;
    Session& session_;
    Config cfg_;
    Stats stats_;
    EntityRegistry reg_;

    bool active_ = false;
    bool localSpawnSent_ = false;
    uint32_t localNetId_ = 0;
    Frame localFrame_;
    bool haveLocalFrame_ = false;
    uint32_t lastSnapshotMs_ = 0;
    uint32_t lastFrameSendMs_ = 0;
    uint32_t lastAliveCheckMs_ = 0;
    uint32_t lastDriveMs_ = 0;
    Frame remoteFrame_;
    bool haveRemoteFrame_ = false;
};

} // namespace osmp
