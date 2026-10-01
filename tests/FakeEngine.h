// FakeEngine.h - in-memory IEngine for testing the replication loop without Spore.
#pragma once

#include "game/EngineFacade.h"
#include <map>
#include <vector>

namespace osmp {

class FakeEngine : public IEngine {
public:
    struct Ghost { EntitySpawn spec; Pose pose; bool alive = true; uint32_t drives = 0; GhostDriveMode lastMode = GhostDriveMode::MoveTo; };

    bool inSpace = true;
    Frame frame;
    LocalShipInfo ship;
    Pose shipPose;
    uint32_t clock = 10000;
    bool failSpawns = false;
    std::vector<std::string> logs;
    std::map<void*, Ghost> ghosts;
    uint32_t nextHandle = 1;

    FakeEngine() { frame.kind = FrameKind::System; frame.starId = 0x1234; ship.politicalId = 0x800021CA; ship.ufoType = 0; ship.model = {1, 2, 3}; shipPose.rot = {0, 0, 0, 1}; }

    bool isInSpaceStage() override { return inSpace; }
    bool getFrame(Frame& out) override { if (!inSpace) return false; out = frame; return true; }
    bool getLocalShip(LocalShipInfo& info, Pose& pose) override { if (!inSpace) return false; info = ship; pose = shipPose; return true; }
    void* spawnGhost(const EntitySpawn& spec, uint32_t, std::string& err) override {
        if (failSpawns) { err = "fake failure"; return nullptr; }
        void* h = (void*)(uintptr_t)(nextHandle++);
        Ghost g; g.spec = spec; g.pose.pos = spec.pos; g.pose.rot = spec.rot;
        ghosts[h] = g;
        return h;
    }
    void driveGhost(void* h, const Pose& target, GhostDriveMode mode, float) override {
        auto it = ghosts.find(h); if (it == ghosts.end() || !it->second.alive) return;
        it->second.pose = target; it->second.drives++; it->second.lastMode = mode;
    }
    bool getGhostPose(void* h, Pose& out) override { auto it = ghosts.find(h); if (it == ghosts.end()) return false; out = it->second.pose; return true; }
    bool isGhostAlive(void* h) override { auto it = ghosts.find(h); return it != ghosts.end() && it->second.alive; }
    void destroyGhost(void* h) override { ghosts.erase(h); }
    uint32_t nowMs() override { return clock; }
    void log(const std::string& line) override { logs.push_back(line); }

    size_t aliveGhosts() const { size_t n = 0; for (auto& kv : ghosts) if (kv.second.alive) n++; return n; }
};

} // namespace osmp
