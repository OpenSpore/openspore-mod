// ModApiEngine.h - IEngine implemented with the Spore ModAPI SDK types (MSVC only).
#pragma once

#include "stdafx.h"
#include "game/EngineFacade.h"

#include <Spore/Simulator/cGameDataUFO.h>
#include <EASTL/map.h>

namespace osmp {

class ModApiEngine : public IEngine {
public:
    struct Options {
        int moveToEveryTicks = 5;
        float moveToGoalStop = 1.0f;
        float moveToAcceptableStop = 4.0f;
        bool neutraliseAI = true;      // mNPCFollowUFO = 0, mNPCFlockIndex = -1 (freezes NPC steering, measured on the ally)
        bool clearBehaviors = false;   // also empty cBehaviorList::mData (untested hypothesis)
        bool useSpawnUFO = true;       // Simulator::SpawnUFO (native path with view + AI) vs CreateUFO
        float velocityGain = 2.0f;
        float maxSpeed = 400.0f;
    };

    using LogFn = void (*)(const char*);
    explicit ModApiEngine(LogFn log) : log_(log) {}

    bool isInSpaceStage() override;
    bool getFrame(Frame& out) override;
    bool getLocalShip(LocalShipInfo& info, Pose& pose) override;
    void* spawnGhost(const EntitySpawn& spec, uint32_t localPoliticalId, std::string& err) override;
    void driveGhost(void* handle, const Pose& target, GhostDriveMode mode, float dtSec) override;
    bool getGhostPose(void* handle, Pose& out) override;
    bool isGhostAlive(void* handle) override;
    void destroyGhost(void* handle) override;
    uint32_t nowMs() override;
    void log(const std::string& line) override { if (log_) log_(line.c_str()); }

    Options& options() { return opt_; }
    void describe(Simulator::cGameDataUFO* ufo, const char* what);

private:
    Simulator::cGameDataUFO* ghost(void* handle);

    LogFn log_;
    Options opt_;
    uint32_t tick_ = 0;
    // intrusive_ptr keeps the refcount up: an unowned noun is collected by the engine (measured: refcount 0 -> vanished)
    eastl::map<void*, cGameDataUFOPtr> ghosts_;
};

} // namespace osmp
