// GhostLab.h - single-machine experiment: spawn a ghost next to the local ship and drive it
// in a circle with the selected GhostDriveMode. No network needed. This is the fastest way
// to answer the open M3 question "does a native-spawned NPC UFO accept our movement commands".
#pragma once

#include "EngineFacade.h"
#include <string>

namespace osmp {

class GhostLab {
public:
    explicit GhostLab(IEngine& engine) : engine_(engine) {}

    bool start(GhostDriveMode mode, float radius = 40.0f, float periodSec = 8.0f);
    void stop();
    void setMode(GhostDriveMode mode) { mode_ = mode; }
    bool active() const { return handle_ != nullptr; }
    void update();                         // call every tick
    std::string statusLine() const;

private:
    IEngine& engine_;
    void* handle_ = nullptr;
    GhostDriveMode mode_ = GhostDriveMode::MoveTo;
    float radius_ = 40.0f;
    float periodSec_ = 8.0f;
    uint32_t startMs_ = 0;
    uint32_t lastMs_ = 0;
    uint32_t lastReportMs_ = 0;
    Pose lastTarget_;
    Pose lastActual_;
    float lastError_ = 0;
};

} // namespace osmp
