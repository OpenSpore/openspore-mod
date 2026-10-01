// RawEngine.h - IEngine implemented with raw addresses (no ModAPI SDK), so the lab DLL
// builds with mingw or MSVC alone. Steam/GOG 3.1.0.29 (march2017 address family) only.
#pragma once

#include "game/EngineFacade.h"
#include <cstdint>
#include <functional>

namespace osmp {

class RawEngine : public IEngine {
public:
    using LogFn = std::function<void(const std::string&)>;

    struct Options {
        int moveToEveryTicks = 5;          // MoveTo re-issue interval
        float moveToGoalStop = 1.0f;
        float moveToAcceptableStop = 4.0f;
        bool neutraliseAI = true;          // zero mNPCFollowUFO, mNPCFlockIndex = -1 (freezes NPC steering)
        bool useSpawnUFO = true;           // SpawnUFO (full native path) vs CreateUFO fallback
        float velocityGain = 2.0f;         // Velocity mode: v = gain * (target - actual) + targetVel
        float maxSpeed = 400.0f;
    };

    RawEngine(uintptr_t moduleBase, LogFn log);

    uintptr_t rt(uintptr_t preferred) const { return base_ + (preferred - 0x400000u); }
    bool selfTest();

    // IEngine
    bool isInSpaceStage() override;
    bool getFrame(Frame& out) override;
    bool getLocalShip(LocalShipInfo& info, Pose& pose) override;
    void* spawnGhost(const EntitySpawn& spec, uint32_t localPoliticalId, std::string& err) override;
    void driveGhost(void* handle, const Pose& target, GhostDriveMode mode, float dtSec) override;
    bool getGhostPose(void* handle, Pose& out) override;
    bool isGhostAlive(void* handle) override;
    void destroyGhost(void* handle) override;
    uint32_t nowMs() override;
    void log(const std::string& line) override { if (log_) log_(line); }

    // lab extras
    uintptr_t playerShip();
    uintptr_t spaceGame();
    bool isNounAlive(uintptr_t obj);
    void describeObject(uintptr_t obj, const char* what);
    Options& options() { return opt_; }

    static bool readable(const void* p, size_t n);
    template <typename T> bool read(uintptr_t addr, T& out) const;
    template <typename T> bool write(uintptr_t addr, const T& v) const;

private:
    bool readPose(uintptr_t ufo, Pose& out) const;
    void neutraliseAI(uintptr_t ufo);
    bool addRef(uintptr_t obj);

    uintptr_t base_;
    LogFn log_;
    Options opt_;
    uint32_t tick_ = 0;
    uintptr_t shipVtable_ = 0;
};

} // namespace osmp
