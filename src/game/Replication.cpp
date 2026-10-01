#include "Replication.h"

#include <cstdio>

namespace osmp {

const char* ghostDriveModeName(GhostDriveMode m) {
    switch (m) {
    case GhostDriveMode::MoveTo: return "moveto";
    case GhostDriveMode::Teleport: return "teleport";
    case GhostDriveMode::SetPosition: return "setpos";
    case GhostDriveMode::Velocity: return "velocity";
    case GhostDriveMode::RawWrite: return "raw";
    }
    return "?";
}

bool parseGhostDriveMode(const std::string& text, GhostDriveMode& out) {
    if (text == "moveto") { out = GhostDriveMode::MoveTo; return true; }
    if (text == "teleport") { out = GhostDriveMode::Teleport; return true; }
    if (text == "setpos") { out = GhostDriveMode::SetPosition; return true; }
    if (text == "velocity") { out = GhostDriveMode::Velocity; return true; }
    if (text == "raw") { out = GhostDriveMode::RawWrite; return true; }
    return false;
}

Replication::Replication(IEngine& engine, Session& session, const Config& cfg)
    : engine_(engine), session_(session), cfg_(cfg) {}

uint32_t Replication::renderDelayMs() const {
    if (cfg_.renderDelayMs) return cfg_.renderDelayMs;
    uint8_t hz = session_.config().snapshotHz ? session_.config().snapshotHz : 15;
    return 2000u / hz;
}

void Replication::onConnected() {
    active_ = true;
    localSpawnSent_ = false;
    localNetId_ = 0;
    haveLocalFrame_ = false;
    haveRemoteFrame_ = false;
    lastSnapshotMs_ = 0;
    engine_.log("replication: session connected, waiting for the space stage");
}

void Replication::onDisconnected() {
    active_ = false;
    for (auto& kv : reg_.all()) destroyGhost(kv.second);
    reg_.clear();
    localSpawnSent_ = false;
    engine_.log("replication: session ended, ghosts removed");
}

void Replication::setGhostsEnabled(bool on) {
    cfg_.ghostsEnabled = on;
    if (!on) for (auto& kv : reg_.all()) destroyGhost(kv.second);
}

bool Replication::remoteGhostVisible() const {
    for (auto& kv : reg_.all()) if (kv.second.spawned) return true;
    return false;
}

void Replication::onMessage(MsgType type, ByteReader& payload) {
    switch (type) {
    case MsgType::EntitySpawn: handleSpawn(payload); break;
    case MsgType::EntityDespawn: handleDespawn(payload); break;
    case MsgType::FrameChange: handleFrameChange(payload); break;
    case MsgType::Transform: handleTransform(payload); break;
    default: break;
    }
}

void Replication::sendLocalSpawn(const LocalShipInfo& info, const Pose& pose) {
    if (!localNetId_) localNetId_ = reg_.allocate(session_.localPlayerId());
    EntitySpawn s;
    s.netId = localNetId_;
    s.ownerPlayerId = session_.localPlayerId();
    s.kind = EntityKind::UFO;
    s.ufoType = info.ufoType;
    s.politicalId = info.politicalId;
    s.model = info.model;
    s.frame = localFrame_;
    s.pos = pose.pos;
    s.rot = pose.rot;
    if (session_.send(MsgType::EntitySpawn, toBytes(s))) {
        localSpawnSent_ = true;
        stats_.spawnsSent++;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "replication: announced local ship netId=%08X type=%u pol=%08X frame=%s",
                      s.netId, (unsigned)s.ufoType, s.politicalId, frameKindName(s.frame.kind));
        engine_.log(buf);
    }
}

void Replication::sampleAndSendLocal(uint32_t nowMs) {
    Frame frame;
    if (!engine_.getFrame(frame)) return;
    LocalShipInfo info;
    Pose pose;
    if (!engine_.getLocalShip(info, pose)) return;

    bool frameChanged = !haveLocalFrame_ || frame != localFrame_;
    localFrame_ = frame;
    haveLocalFrame_ = true;

    if (!localSpawnSent_) {
        sendLocalSpawn(info, pose);
        lastFrameSendMs_ = nowMs;
        return;
    }

    if (frameChanged || nowMs - lastFrameSendMs_ >= cfg_.frameResendMs) {
        FrameChange fc;
        fc.playerId = session_.localPlayerId();
        fc.frame = frame;
        if (session_.send(MsgType::FrameChange, toBytes(fc))) { stats_.frameChangesSent++; lastFrameSendMs_ = nowMs; }
        if (frameChanged) {
            char buf[120];
            std::snprintf(buf, sizeof(buf), "replication: local frame -> %s star=%08X planet=%08X", frameKindName(frame.kind), frame.starId, frame.planetId);
            engine_.log(buf);
        }
    }

    uint8_t hz = session_.config().snapshotHz ? session_.config().snapshotHz : 15;
    if (nowMs - lastSnapshotMs_ >= 1000u / hz) {
        lastSnapshotMs_ = nowMs;
        Transform t;
        t.netId = localNetId_;
        t.tick = nowMs;
        t.frame = frame;
        t.pos = pose.pos;
        t.rot = pose.rot;
        t.vel = pose.vel;
        if (session_.send(MsgType::Transform, toBytes(t))) stats_.snapshotsSent++;
    }
}

void Replication::handleSpawn(ByteReader& r) {
    EntitySpawn s;
    if (!decode(r, s)) return;
    stats_.spawnsRecv++;
    NetEntity* existing = reg_.find(s.netId);
    if (existing) destroyGhost(*existing);
    reg_.add(s);
    remoteFrame_ = s.frame;
    haveRemoteFrame_ = true;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "replication: remote ship netId=%08X owner=%u type=%u pol=%08X frame=%s",
                  s.netId, (unsigned)s.ownerPlayerId, (unsigned)s.ufoType, s.politicalId, frameKindName(s.frame.kind));
    engine_.log(buf);
}

void Replication::handleDespawn(ByteReader& r) {
    EntityDespawn d;
    if (!decode(r, d)) return;
    NetEntity* e = reg_.find(d.netId);
    if (!e) return;
    destroyGhost(*e);
    reg_.remove(d.netId);
}

void Replication::handleFrameChange(ByteReader& r) {
    FrameChange fc;
    if (!decode(r, fc)) return;
    stats_.frameChangesRecv++;
    bool changed = !haveRemoteFrame_ || fc.frame != remoteFrame_;
    remoteFrame_ = fc.frame;
    haveRemoteFrame_ = true;
    for (auto& kv : reg_.all()) {
        if (kv.second.ownerPlayerId == fc.playerId && kv.second.frame != fc.frame) {
            kv.second.frame = fc.frame;
            kv.second.interp.clear();
        }
    }
    if (changed) {
        char buf[120];
        std::snprintf(buf, sizeof(buf), "replication: remote frame -> %s star=%08X planet=%08X", frameKindName(fc.frame.kind), fc.frame.starId, fc.frame.planetId);
        engine_.log(buf);
    }
}

void Replication::handleTransform(ByteReader& r) {
    Transform t;
    if (!decode(r, t)) return;
    if (reg_.applyTransform(t, engine_.nowMs())) {
        stats_.snapshotsRecv++;
        remoteFrame_ = t.frame;
        haveRemoteFrame_ = true;
    }
}

void Replication::destroyGhost(NetEntity& e) {
    if (!e.spawned) return;
    if (e.handle && engine_.isGhostAlive(e.handle)) engine_.destroyGhost(e.handle);
    e.handle = nullptr;
    e.spawned = false;
    stats_.ghostsDestroyed++;
}

void Replication::reconcileGhosts(uint32_t nowMs) {
    bool inSpace = engine_.isInSpaceStage();
    bool checkAlive = nowMs - lastAliveCheckMs_ >= cfg_.ghostAliveCheckMs;
    if (checkAlive) lastAliveCheckMs_ = nowMs;

    for (auto& kv : reg_.all()) {
        NetEntity& e = kv.second;
        if (e.ownerPlayerId == session_.localPlayerId()) continue;

        if (e.spawned && checkAlive && !engine_.isGhostAlive(e.handle)) {
            engine_.log("replication: engine dropped the ghost, will respawn");
            e.handle = nullptr;
            e.spawned = false;
            stats_.ghostsLostByEngine++;
        }

        bool sameFrame = haveLocalFrame_ && e.frame == localFrame_;
        bool wantGhost = cfg_.ghostsEnabled && inSpace && sameFrame;
        if (wantGhost && !e.spawned) {
            EntitySpawn spec;
            spec.netId = e.netId;
            spec.ownerPlayerId = e.ownerPlayerId;
            spec.kind = e.kind;
            spec.ufoType = e.ufoType;
            spec.politicalId = e.politicalId;
            spec.model = e.model;
            spec.frame = e.frame;
            spec.pos = e.last.pos;
            spec.rot = e.last.rot;
            LocalShipInfo info; Pose pose;
            uint32_t localPol = engine_.getLocalShip(info, pose) ? info.politicalId : 0;
            std::string err;
            void* h = engine_.spawnGhost(spec, cfg_.useOwnPoliticalId ? localPol : spec.politicalId, err);
            if (h) {
                e.handle = h;
                e.spawned = true;
                stats_.ghostsSpawned++;
                char buf[120];
                std::snprintf(buf, sizeof(buf), "replication: ghost spawned for netId=%08X (%s)", e.netId, ghostDriveModeName(cfg_.driveMode));
                engine_.log(buf);
            } else {
                stats_.ghostSpawnFailures++;
                engine_.log("replication: ghost spawn failed: " + err);
            }
        } else if (!wantGhost && e.spawned) {
            engine_.log(sameFrame ? "replication: ghost removed (space stage left / disabled)" : "replication: ghost removed (different frame)");
            destroyGhost(e);
        }
    }
}

void Replication::driveGhosts(uint32_t nowMs) {
    float dt = lastDriveMs_ ? (nowMs - lastDriveMs_) / 1000.0f : 0.033f;
    lastDriveMs_ = nowMs;
    uint32_t renderTime = nowMs - renderDelayMs();
    for (auto& kv : reg_.all()) {
        NetEntity& e = kv.second;
        if (!e.spawned || e.interp.empty()) continue;
        Pose target;
        // Snapshot ticks are the sender's clock; convert the render time to it using the arrival offset.
        uint32_t senderNow = e.last.tick + (nowMs - e.lastUpdateMs);
        uint32_t senderRender = senderNow - renderDelayMs();
        if (!e.interp.sample(senderRender, target.pos, target.rot)) continue;
        target.vel = e.last.vel;
        (void)renderTime;
        engine_.driveGhost(e.handle, target, cfg_.driveMode, dt);
    }
}

void Replication::update() {
    if (!active_ || !session_.isConnected()) return;
    uint32_t nowMs = engine_.nowMs();
    if (engine_.isInSpaceStage()) sampleAndSendLocal(nowMs);
    reconcileGhosts(nowMs);
    driveGhosts(nowMs);
}

std::string Replication::statusLine() const {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "session=%s role=%s player=%u rtt=%ums | local netId=%08X frame=%s | remote entities=%zu ghost=%s | snap tx/rx=%u/%u spawn tx/rx=%u/%u ghosts +%u -%u fail=%u lost=%u | mode=%s",
                  Session::stateName(session_.state()),
                  session_.role() == Session::Role::Host ? "host" : (session_.role() == Session::Role::Client ? "client" : "none"),
                  (unsigned)session_.localPlayerId(), session_.stats().rttMs,
                  localNetId_, frameKindName(localFrame_.kind),
                  reg_.size(), remoteGhostVisible() ? "visible" : "none",
                  stats_.snapshotsSent, stats_.snapshotsRecv, stats_.spawnsSent, stats_.spawnsRecv,
                  stats_.ghostsSpawned, stats_.ghostsDestroyed, stats_.ghostSpawnFailures, stats_.ghostsLostByEngine,
                  ghostDriveModeName(cfg_.driveMode));
    return buf;
}

} // namespace osmp
