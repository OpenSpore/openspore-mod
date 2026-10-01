#include "GhostLab.h"
#include "net/Interpolation.h"

#include <cmath>
#include <cstdio>

namespace osmp {

bool GhostLab::start(GhostDriveMode mode, float radius, float periodSec) {
    stop();
    LocalShipInfo info; Pose pose;
    if (!engine_.isInSpaceStage() || !engine_.getLocalShip(info, pose)) {
        engine_.log("ghostlab: not in space stage / no local ship");
        return false;
    }
    Frame frame;
    engine_.getFrame(frame);
    EntitySpawn spec;
    spec.netId = 0xFF000001u;             // lab entity, never sent anywhere
    spec.ownerPlayerId = 0xFF;
    spec.kind = EntityKind::UFO;
    spec.ufoType = info.ufoType ? info.ufoType : 2;   // Raider has a model and full AI; AI is neutralised by the engine layer
    spec.politicalId = info.politicalId;
    spec.model = info.model;
    spec.frame = frame;
    spec.pos = pose.pos; spec.pos.x += radius;
    spec.rot = pose.rot;
    std::string err;
    handle_ = engine_.spawnGhost(spec, info.politicalId, err);
    if (!handle_) { engine_.log("ghostlab: spawn failed: " + err); return false; }
    mode_ = mode; radius_ = radius; periodSec_ = periodSec;
    startMs_ = lastMs_ = engine_.nowMs();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "ghostlab: ghost spawned, mode=%s radius=%.0f period=%.1fs", ghostDriveModeName(mode), radius, periodSec);
    engine_.log(buf);
    return true;
}

void GhostLab::stop() {
    if (handle_ && engine_.isGhostAlive(handle_)) engine_.destroyGhost(handle_);
    handle_ = nullptr;
}

void GhostLab::update() {
    if (!handle_) return;
    if (!engine_.isGhostAlive(handle_)) { engine_.log("ghostlab: engine collected the ghost"); handle_ = nullptr; return; }
    LocalShipInfo info; Pose ship;
    if (!engine_.getLocalShip(info, ship)) return;
    uint32_t now = engine_.nowMs();
    float dt = (now - lastMs_) / 1000.0f;
    lastMs_ = now;
    float t = (now - startMs_) / 1000.0f;
    float a = 6.2831853f * t / periodSec_;
    Pose target;
    target.pos.x = ship.pos.x + radius_ * std::cos(a);
    target.pos.y = ship.pos.y + radius_ * std::sin(a);
    target.pos.z = ship.pos.z;
    float w = 6.2831853f / periodSec_;
    target.vel.x = -radius_ * w * std::sin(a);
    target.vel.y = radius_ * w * std::cos(a);
    target.vel.z = 0;
    // face along the tangent: yaw about Z
    float yaw = a + 1.5707963f;
    target.rot.x = 0; target.rot.y = 0; target.rot.z = std::sin(yaw / 2); target.rot.w = std::cos(yaw / 2);
    engine_.driveGhost(handle_, target, mode_, dt);
    lastTarget_ = target;
    if (now - lastReportMs_ >= 1000) {
        lastReportMs_ = now;
        Pose actual;
        if (engine_.getGhostPose(handle_, actual)) {
            lastActual_ = actual;
            Vec3 d{actual.pos.x - target.pos.x, actual.pos.y - target.pos.y, actual.pos.z - target.pos.z};
            lastError_ = length(d);
            char buf[200];
            std::snprintf(buf, sizeof(buf), "ghostlab[%s]: target (%.1f %.1f %.1f) actual (%.1f %.1f %.1f) err=%.1f",
                          ghostDriveModeName(mode_), target.pos.x, target.pos.y, target.pos.z, actual.pos.x, actual.pos.y, actual.pos.z, lastError_);
            engine_.log(buf);
        }
    }
}

std::string GhostLab::statusLine() const {
    if (!handle_) return "ghostlab: inactive";
    char buf[160];
    std::snprintf(buf, sizeof(buf), "ghostlab: active mode=%s err=%.1f target=(%.1f %.1f %.1f)", ghostDriveModeName(mode_), lastError_, lastTarget_.pos.x, lastTarget_.pos.y, lastTarget_.pos.z);
    return buf;
}

} // namespace osmp
