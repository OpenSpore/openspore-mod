#include "RawEngine.h"
#include "RawAddresses.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace osmp {

using namespace raw;

namespace {
struct RawVec3 { float x, y, z; };
struct RawQuat { float x, y, z, w; };
struct RawResKey { uint32_t instanceID, typeID, groupID; };

typedef void* (__cdecl* SpawnUFO_t)(int type, const uint32_t* politicalId, const RawResKey* modelKey);
typedef void* (__cdecl* CreateUFO_t)(int type, void* empire);
typedef void* (__cdecl* GetPlayerEmpire_t)();
typedef void* (__cdecl* NounMgrGet_t)();
typedef void  (__thiscall* DestroyInstance_t)(void* self, void* obj);
typedef int   (__thiscall* AddRef_t)(void* self);
typedef void  (__thiscall* MoveTo_t)(void* loco, const RawVec3* dst, float goalStop, float acceptableStop, bool flag);
typedef void  (__thiscall* Teleport_t)(void* spatial, const RawVec3* pos, const RawQuat* rot);
typedef void  (__thiscall* SetPosition_t)(void* spatial, const RawVec3* pos);
typedef void  (__thiscall* SetOrientation_t)(void* spatial, const RawQuat* rot);
typedef void  (__thiscall* SetDesiredSpeed_t)(void* loco, float speed, int);
} // namespace

bool RawEngine::readable(const void* p, size_t n) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    DWORD prot = mbi.Protect & 0xFF;
    if (prot == PAGE_NOACCESS || prot == PAGE_EXECUTE || (mbi.Protect & PAGE_GUARD)) return false;
    uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    return (uintptr_t)p + n <= end;
}

template <typename T> bool RawEngine::read(uintptr_t addr, T& out) const {
    if (!readable((const void*)addr, sizeof(T))) return false;
    std::memcpy(&out, (const void*)addr, sizeof(T));
    return true;
}

template <typename T> bool RawEngine::write(uintptr_t addr, const T& v) const {
    if (!readable((const void*)addr, sizeof(T))) return false;
    DWORD old = 0;
    if (!VirtualProtect((void*)addr, sizeof(T), PAGE_READWRITE, &old)) return false;
    std::memcpy((void*)addr, &v, sizeof(T));
    VirtualProtect((void*)addr, sizeof(T), old, &old);
    return true;
}

RawEngine::RawEngine(uintptr_t moduleBase, LogFn log) : base_(moduleBase), log_(std::move(log)) {}

bool RawEngine::selfTest() {
    uintptr_t fn = rt(kGetClassNameFn);
    unsigned char op = 0;
    if (!read(fn, op) || op != 0xB8) {
        char b[96]; std::snprintf(b, sizeof(b), "selftest: opcode %02X at %08X, expected B8 (mov eax, imm32)", op, (unsigned)fn);
        log(b);
        return false;
    }
    uintptr_t strPtr = 0;
    read(fn + 1, strPtr);
    char buf[32] = {};
    for (int i = 0; i < 31; ++i) { char c = 0; if (!read(strPtr + i, c) || !c) break; buf[i] = c; }
    bool ok = std::strcmp(buf, "cSimulatorSpaceGame") == 0;
    char b[128]; std::snprintf(b, sizeof(b), "selftest: %08X -> \"%s\" %s", (unsigned)strPtr, buf, ok ? "MATCH" : "MISMATCH");
    log(b);
    return ok;
}

uintptr_t RawEngine::spaceGame() {
    uintptr_t sg = 0;
    read(rt(kSpaceGameSingleton), sg);
    return sg;
}

uintptr_t RawEngine::playerShip() {
    uintptr_t sim = 0;
    if (!read(rt(kPlayerUfoSingleton), sim) || !sim) return 0;
    uintptr_t ship = 0;
    if (!read(sim + PlayerUfoSim::mpPlayerUFO, ship)) return 0;
    if (ship && !shipVtable_) read(ship, shipVtable_);
    return ship;
}

bool RawEngine::isInSpaceStage() {
    return spaceGame() != 0 && playerShip() != 0;
}

bool RawEngine::getFrame(Frame& out) {
    uintptr_t spd = 0;
    if (!read(rt(kSpacePlayerDataPtr), spd) || !spd) return false;
    int context = -1;
    read(spd + SpacePlayerData::mCurrentContext, context);
    out = Frame();
    switch (context) {
    case 0: out.kind = FrameKind::Planet; break;
    case 1: out.kind = FrameKind::System; break;
    case 2: out.kind = FrameKind::Galaxy; break;
    default: out.kind = FrameKind::None; break;
    }
    uintptr_t star = 0;
    if (read(spd + SpacePlayerData::mpActiveStar, star) && star) read(star + Star::mKey, out.starId);
    if (out.kind == FrameKind::Planet) {
        uintptr_t planet = 0;
        if (read(spd + SpacePlayerData::mpActivePlanet, planet) && planet) {
            uintptr_t rec = 0;
            if (read(planet + Planet::mpPlanetRecord, rec) && rec) read(rec + PlanetRecord::mKey, out.planetId);
            if (!out.planetId) read(planet + Planet::mPlanetKey, out.planetId);
        }
    }
    return true;
}

bool RawEngine::readPose(uintptr_t ufo, Pose& out) const {
    RawVec3 p; RawQuat q; RawVec3 v;
    if (!read(ufo + Ufo::position, p) || !read(ufo + Ufo::orientation, q)) return false;
    if (!read(ufo + Ufo::velocity, v)) v = {0, 0, 0};
    out.pos = {p.x, p.y, p.z};
    out.rot = {q.x, q.y, q.z, q.w};
    out.vel = {v.x, v.y, v.z};
    return true;
}

bool RawEngine::getLocalShip(LocalShipInfo& info, Pose& pose) {
    uintptr_t ship = playerShip();
    if (!ship) return false;
    if (!readPose(ship, pose)) return false;
    read(ship + GameData::mPoliticalID, info.politicalId);
    int type = 0; read(ship + Ufo::mUFOType, type); info.ufoType = (uint8_t)type;
    RawResKey key = {0, 0, 0};
    if (read(ship + Ufo::modelKey, key)) { info.model.instanceId = key.instanceID; info.model.typeId = key.typeID; info.model.groupId = key.groupID; }
    return true;
}

bool RawEngine::addRef(uintptr_t obj) {
    uintptr_t vt = 0, fn = 0;
    if (!read(obj, vt) || !read(vt + 4, fn) || !fn) return false;
    ((AddRef_t)fn)((void*)obj);
    return true;
}

void RawEngine::neutraliseAI(uintptr_t ufo) {
    // Measured on the posse ally: zeroing the follow target and the flock index froze its AI steering.
    int zero = 0, minusOne = -1;
    write(ufo + Ufo::mNPCFollowUFO, zero);
    write(ufo + Ufo::mNPCFlockIndex, minusOne);
}

void* RawEngine::spawnGhost(const EntitySpawn& spec, uint32_t localPoliticalId, std::string& err) {
    uintptr_t ship = playerShip();
    if (!ship) { err = "no player ship"; return nullptr; }

    RawResKey model = {spec.model.instanceId, spec.model.typeId, spec.model.groupId};
    if (!model.instanceID) read(ship + Ufo::modelKey, model);   // same model as ourselves until creations are exchanged
    uint32_t pol = localPoliticalId ? localPoliticalId : spec.politicalId;
    int type = spec.ufoType ? spec.ufoType : 2;

    void* obj = nullptr;
    if (opt_.useSpawnUFO) {
        obj = ((SpawnUFO_t)rt(kSpawnUFO))(type, &pol, &model);
        char b[160]; std::snprintf(b, sizeof(b), "spawn: SpawnUFO(type=%d pol=%08X model=%08X:%08X:%08X) -> %p", type, pol, model.instanceID, model.typeID, model.groupID, obj);
        log(b);
    }
    if (!obj) {
        void* empire = ((GetPlayerEmpire_t)rt(kGetPlayerEmpire))();
        obj = ((CreateUFO_t)rt(kCreateUFO))(type, empire);
        char b[120]; std::snprintf(b, sizeof(b), "spawn: CreateUFO(type=%d empire=%p) -> %p", type, empire, obj);
        log(b);
    }
    if (!obj) { err = "SpawnUFO and CreateUFO both returned null"; return nullptr; }

    uintptr_t o = (uintptr_t)obj;
    addRef(o);                       // pin it: without an owner the engine collects the object
    if (opt_.neutraliseAI) neutraliseAI(o);

    // Place it where the remote ship is: raw write of the "next" fields, then let the drive mode take over.
    RawVec3 p = {spec.pos.x, spec.pos.y, spec.pos.z};
    RawQuat q = {spec.rot.x, spec.rot.y, spec.rot.z, spec.rot.w};
    write(o + Ufo::mNextPosition, p);
    write(o + Ufo::mDestination, p);
    write(o + Ufo::mNextOrientation, q);
    describeObject(o, "ghost");
    return obj;
}

void RawEngine::describeObject(uintptr_t obj, const char* what) {
    uintptr_t vt = 0, view = 0; uint32_t rc = 0, id = 0, pol = 0; int type = 0;
    read(obj, vt); read(obj + GameData::mpView, view); read(obj + GameData::refCount, rc);
    read(obj + GameData::mID, id); read(obj + GameData::mPoliticalID, pol); read(obj + Ufo::mUFOType, type);
    Pose pose; readPose(obj, pose);
    char b[240];
    std::snprintf(b, sizeof(b), "%s: obj=%08X vtable=%08X%s mpView=%08X refs=%u mID=%08X pol=%08X type=%d pos=(%.1f %.1f %.1f)",
                  what, (unsigned)obj, (unsigned)vt, (shipVtable_ && vt == shipVtable_) ? "(=player ship class)" : "",
                  (unsigned)view, rc, id, pol, type, pose.pos.x, pose.pos.y, pose.pos.z);
    log(b);
}

void RawEngine::driveGhost(void* handle, const Pose& target, GhostDriveMode mode, float dtSec) {
    uintptr_t o = (uintptr_t)handle;
    if (!o) return;
    ++tick_;
    uintptr_t loco = o + Ufo::locomotiveBase;
    uintptr_t vt = 0;
    if (!read(loco, vt) || !vt) return;
    RawVec3 p = {target.pos.x, target.pos.y, target.pos.z};
    RawQuat q = {target.rot.x, target.rot.y, target.rot.z, target.rot.w};

    switch (mode) {
    case GhostDriveMode::MoveTo: {
        if (opt_.moveToEveryTicks > 1 && (tick_ % opt_.moveToEveryTicks) != 0) return;
        uintptr_t fn = 0; read(vt + LocoVt::MoveTo, fn); if (!fn) return;
        // extrapolate half a re-issue interval ahead so the ghost does not lag behind its own goal
        float ahead = dtSec * opt_.moveToEveryTicks * 0.5f;
        RawVec3 goal = {p.x + target.vel.x * ahead, p.y + target.vel.y * ahead, p.z + target.vel.z * ahead};
        ((MoveTo_t)fn)((void*)loco, &goal, opt_.moveToGoalStop, opt_.moveToAcceptableStop, false);
        float speed = std::sqrt(target.vel.x * target.vel.x + target.vel.y * target.vel.y + target.vel.z * target.vel.z);
        uintptr_t sds = 0; read(vt + LocoVt::SetDesiredSpeed, sds);
        if (sds && speed > 0.1f) ((SetDesiredSpeed_t)sds)((void*)loco, speed, 0);
        break;
    }
    case GhostDriveMode::Teleport: {
        uintptr_t fn = 0; read(vt + SpatialVt::Teleport, fn); if (!fn) return;
        ((Teleport_t)fn)((void*)loco, &p, &q);
        break;
    }
    case GhostDriveMode::SetPosition: {
        uintptr_t fp = 0, fo = 0; read(vt + SpatialVt::SetPosition, fp); read(vt + SpatialVt::SetOrientation, fo);
        if (fp) ((SetPosition_t)fp)((void*)loco, &p);
        if (fo) ((SetOrientation_t)fo)((void*)loco, &q);
        break;
    }
    case GhostDriveMode::Velocity: {
        Pose actual;
        if (!readPose(o, actual)) return;
        RawVec3 v = {opt_.velocityGain * (p.x - actual.pos.x) + target.vel.x,
                     opt_.velocityGain * (p.y - actual.pos.y) + target.vel.y,
                     opt_.velocityGain * (p.z - actual.pos.z) + target.vel.z};
        float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (speed > opt_.maxSpeed) { float s = opt_.maxSpeed / speed; v.x *= s; v.y *= s; v.z *= s; }
        write(o + Ufo::velocity, v);          // cLocomotiveObject::mVelocity (what ModAPI's SetVelocity writes)
        write(o + Ufo::mNextVelocity, v);
        write(o + Ufo::mNextOrientation, q);
        break;
    }
    case GhostDriveMode::RawWrite: {
        uint8_t zero = 0;
        write(o + Ufo::position, p);
        write(o + Ufo::mNextPosition, p);
        write(o + Ufo::mDestination, p);
        write(o + Ufo::orientation, q);
        write(o + Ufo::mNextOrientation, q);
        write(o + Ufo::mbAtDestination, zero);
        break;
    }
    }
}

bool RawEngine::getGhostPose(void* handle, Pose& out) {
    return handle && readPose((uintptr_t)handle, out);
}

bool RawEngine::isNounAlive(uintptr_t obj) {
    void* mgr = ((NounMgrGet_t)rt(kNounManagerGet))();
    if (!mgr) return false;
    uintptr_t sentinel = (uintptr_t)mgr + NounManager::mNouns;
    uintptr_t node = 0;
    if (!read(sentinel, node)) return false;
    for (int guard = 0; node && node != sentinel && guard < 8000; ++guard) {
        if (node - NounManager::nodeToObject == obj) return true;
        if (!read(node, node)) return false;
    }
    return false;
}

bool RawEngine::isGhostAlive(void* handle) {
    if (!handle) return false;
    uintptr_t o = (uintptr_t)handle;
    uintptr_t vt = 0;
    if (!read(o, vt) || !vt) return false;
    if (shipVtable_ && vt != shipVtable_) return false;   // memory reused by another class
    return isNounAlive(o);
}

void RawEngine::destroyGhost(void* handle) {
    if (!handle) return;
    void* mgr = ((NounMgrGet_t)rt(kNounManagerGet))();
    if (!mgr) return;
    ((DestroyInstance_t)rt(kNounDestroyInstance))(mgr, handle);
    char b[64]; std::snprintf(b, sizeof(b), "destroy: DestroyInstance(%p)", handle);
    log(b);
}

uint32_t RawEngine::nowMs() { return (uint32_t)GetTickCount(); }

// explicit instantiations used by LabMain
template bool RawEngine::read<uintptr_t>(uintptr_t, uintptr_t&) const;
template bool RawEngine::read<int>(uintptr_t, int&) const;
template bool RawEngine::read<float>(uintptr_t, float&) const;
template bool RawEngine::read<unsigned char>(uintptr_t, unsigned char&) const;
template bool RawEngine::read<char>(uintptr_t, char&) const;

} // namespace osmp
