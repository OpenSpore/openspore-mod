#include "ModApiEngine.h"

#include <Spore/Simulator/cSimulatorSpaceGame.h>
#include <Spore/Simulator/cSimulatorPlayerUFO.h>
#include <Spore/Simulator/cStar.h>
#include <Spore/Simulator/cPlanet.h>
#include <Spore/Simulator/cStarRecord.h>
#include <Spore/Simulator/cPlanetRecord.h>
#include <Spore/Simulator/cEmpire.h>
#include <Spore/Simulator/SubSystem/SpacePlayerData.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>

#include <cmath>
#include <cstdio>

namespace osmp {

using namespace Simulator;

namespace {
inline Vec3 fromV(const Math::Vector3& v) { return {v.x, v.y, v.z}; }
inline Math::Vector3 toV(const Vec3& v) { return Math::Vector3(v.x, v.y, v.z); }
inline Quat fromQ(const Math::Quaternion& q) { return {q.x, q.y, q.z, q.w}; }
inline Math::Quaternion toQ(const Quat& q) { Math::Quaternion r; r.x = q.x; r.y = q.y; r.z = q.z; r.w = q.w; return r; }
} // namespace

bool ModApiEngine::isInSpaceStage() {
    if (!IsSpaceGame()) return false;
    if (!cSimulatorSpaceGame::Get() || !cSimulatorPlayerUFO::Get()) return false;
    return GetPlayerUFO() != nullptr;
}

bool ModApiEngine::getFrame(Frame& out) {
    SpacePlayerData* spd = SpacePlayerData::Get();
    if (!spd) return false;
    out = Frame();
    switch (spd->mCurrentContext) {
    case SpaceContext::Planet: out.kind = FrameKind::Planet; break;
    case SpaceContext::SolarSystem: out.kind = FrameKind::System; break;
    case SpaceContext::Galaxy: out.kind = FrameKind::Galaxy; break;
    default: out.kind = FrameKind::None; break;
    }
    if (spd->mpActiveStar) out.starId = spd->mpActiveStar->mKey;
    if (out.kind == FrameKind::Planet && spd->mpActivePlanet) {
        cPlanet* planet = spd->mpActivePlanet.get();
        if (planet->mpPlanetRecord) out.planetId = planet->mpPlanetRecord->mKey.internalValue;
        if (!out.planetId) out.planetId = planet->mPlanetKey;
    }
    return true;
}

bool ModApiEngine::getLocalShip(LocalShipInfo& info, Pose& pose) {
    cGameDataUFO* ufo = GetPlayerUFO();
    if (!ufo) return false;
    pose.pos = fromV(ufo->mPosition);
    pose.rot = fromQ(ufo->mOrientation);
    pose.vel = fromV(ufo->mVelocity);
    info.politicalId = ufo->mPoliticalID;
    info.ufoType = (uint8_t)ufo->mUFOType;
    info.model.instanceId = ufo->mModelKey.instanceID;
    info.model.typeId = ufo->mModelKey.typeID;
    info.model.groupId = ufo->mModelKey.groupID;
    return true;
}

cGameDataUFO* ModApiEngine::ghost(void* handle) {
    auto it = ghosts_.find(handle);
    return it == ghosts_.end() ? nullptr : it->second.get();
}

void ModApiEngine::describe(cGameDataUFO* ufo, const char* what) {
    if (!ufo) return;
    char b[240];
    std::snprintf(b, sizeof(b), "%s: obj=%p mpView=%p mID=%08X pol=%08X type=%d destroyed=%d pos=(%.1f %.1f %.1f) behaviors=%u",
                  what, (void*)ufo, (void*)ufo->mpView, ufo->mID, ufo->mPoliticalID, ufo->mUFOType, ufo->mbIsDestroyed ? 1 : 0,
                  ufo->mPosition.x, ufo->mPosition.y, ufo->mPosition.z, (unsigned)ufo->mData.size());
    log(b);
}

void* ModApiEngine::spawnGhost(const EntitySpawn& spec, uint32_t localPoliticalId, std::string& err) {
    cGameDataUFO* player = GetPlayerUFO();
    if (!player) { err = "no player ship"; return nullptr; }

    ResourceKey model(spec.model.instanceId, spec.model.typeId, spec.model.groupId);
    if (!model.instanceID) model = player->mModelKey;     // identical ships until creations are exchanged
    uint32_t pol = localPoliticalId ? localPoliticalId : spec.politicalId;
    UfoType type = (UfoType)(spec.ufoType ? spec.ufoType : 2);

    cGameDataUFO* ufo = nullptr;
    if (opt_.useSpawnUFO) {
        ufo = SpawnUFO(type, pol, model);
        char b[160];
        std::snprintf(b, sizeof(b), "spawn: SpawnUFO(type=%d pol=%08X model=%08X:%08X:%08X) -> %p", (int)type, pol, model.instanceID, model.typeID, model.groupID, (void*)ufo);
        log(b);
    }
    if (!ufo) {
        cEmpire* empire = GetPlayerEmpire();
        ufo = CreateUFO(type, empire);
        char b[120];
        std::snprintf(b, sizeof(b), "spawn: CreateUFO(type=%d empire=%p) -> %p", (int)type, (void*)empire, (void*)ufo);
        log(b);
    }
    if (!ufo) { err = "SpawnUFO and CreateUFO both returned null"; return nullptr; }

    ghosts_[ufo] = cGameDataUFOPtr(ufo);     // AddRef
    if (opt_.neutraliseAI) {
        ufo->mNPCFollowUFO = 0;
        ufo->mNPCFlockIndex = -1;
    }
    if (opt_.clearBehaviors) {
        eastl::vector<cBehaviorBasePtr> copy = ufo->mData;
        for (auto& b : copy) ufo->Remove(b.get());
    }
    ufo->mNextPosition = toV(spec.pos);
    ufo->mDestination = toV(spec.pos);
    ufo->mNextOrientation = toQ(spec.rot);
    describe(ufo, "ghost");
    return ufo;
}

void ModApiEngine::driveGhost(void* handle, const Pose& target, GhostDriveMode mode, float dtSec) {
    cGameDataUFO* ufo = ghost(handle);
    if (!ufo || ufo->mbIsDestroyed) return;
    ++tick_;
    Math::Vector3 p = toV(target.pos);
    Math::Quaternion q = toQ(target.rot);

    switch (mode) {
    case GhostDriveMode::MoveTo: {
        if (opt_.moveToEveryTicks > 1 && (tick_ % opt_.moveToEveryTicks) != 0) return;
        float ahead = dtSec * opt_.moveToEveryTicks * 0.5f;
        Math::Vector3 goal(p.x + target.vel.x * ahead, p.y + target.vel.y * ahead, p.z + target.vel.z * ahead);
        ufo->MoveTo(goal, opt_.moveToGoalStop, opt_.moveToAcceptableStop, false);
        float speed = std::sqrt(target.vel.x * target.vel.x + target.vel.y * target.vel.y + target.vel.z * target.vel.z);
        if (speed > 0.1f) ufo->SetDesiredSpeed(speed, 0);
        break;
    }
    case GhostDriveMode::Teleport:
        ufo->Teleport(p, q);
        break;
    case GhostDriveMode::SetPosition:
        ufo->SetPosition(p);
        ufo->SetOrientation(q);
        break;
    case GhostDriveMode::Velocity: {
        Math::Vector3 v(opt_.velocityGain * (p.x - ufo->mPosition.x) + target.vel.x,
                        opt_.velocityGain * (p.y - ufo->mPosition.y) + target.vel.y,
                        opt_.velocityGain * (p.z - ufo->mPosition.z) + target.vel.z);
        float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (speed > opt_.maxSpeed) { float s = opt_.maxSpeed / speed; v.x *= s; v.y *= s; v.z *= s; }
        ufo->SetVelocity(v);
        ufo->mNextVelocity = v;
        ufo->mNextOrientation = q;
        break;
    }
    case GhostDriveMode::RawWrite:
        ufo->mPosition = p;
        ufo->mNextPosition = p;
        ufo->mDestination = p;
        ufo->mOrientation = q;
        ufo->mNextOrientation = q;
        ufo->mbAtDestination = false;
        break;
    }
}

bool ModApiEngine::getGhostPose(void* handle, Pose& out) {
    cGameDataUFO* ufo = ghost(handle);
    if (!ufo) return false;
    out.pos = fromV(ufo->mPosition);
    out.rot = fromQ(ufo->mOrientation);
    out.vel = fromV(ufo->mVelocity);
    return true;
}

bool ModApiEngine::isGhostAlive(void* handle) {
    cGameDataUFO* ufo = ghost(handle);
    return ufo && !ufo->mbIsDestroyed;
}

void ModApiEngine::destroyGhost(void* handle) {
    cGameDataUFO* ufo = ghost(handle);
    if (!ufo) return;
    if (!ufo->mbIsDestroyed) GameNounManager.DestroyInstance(ufo);
    ghosts_.erase(handle);   // Release
    char b[64]; std::snprintf(b, sizeof(b), "destroy: %p", handle);
    log(b);
}

uint32_t ModApiEngine::nowMs() { return (uint32_t)GetTickCount(); }

} // namespace osmp
