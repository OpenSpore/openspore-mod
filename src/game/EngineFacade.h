// EngineFacade.h - the only thing the replication layer knows about the game.
//
// Three implementations:
//   * FakeEngine   (tests/)          - in-memory, lets the whole replication loop run on Linux
//   * RawEngine    (src/lab/)        - raw addresses from idapro-reverse-dump, builds with mingw
//   * ModApiEngine (src/modapi/)     - Spore ModAPI SDK types, builds with Visual Studio
#pragma once

#include "net/Protocol.h"
#include <string>

namespace osmp {

struct Pose {
    Vec3 pos;
    Quat rot;
    Vec3 vel;
};

struct LocalShipInfo {
    uint32_t politicalId = 0;
    uint8_t ufoType = 0;
    ResKey model;
};

// How a remote player's ghost ship is moved. Several hypotheses are kept on purpose
// (owner's rule: test everything); the mode is switchable at runtime.
enum class GhostDriveMode : uint8_t {
    MoveTo = 0,        // cLocomotiveObject::MoveTo (vtable +0xE0): hand the engine a destination
    Teleport = 1,      // cSpatialObject::Teleport (vtable +0x44): position + orientation, physics reset
    SetPosition = 2,   // cSpatialObject::SetPosition/SetOrientation (vtable +0x38/+0x3C)
    Velocity = 3,      // cLocomotiveObject::SetVelocity towards the target (steering)
    RawWrite = 4,      // write +0x38 / +0x718 / +0x750 directly (known to rubber-band on the player ship)
};

const char* ghostDriveModeName(GhostDriveMode m);
bool parseGhostDriveMode(const std::string& text, GhostDriveMode& out);

class IEngine {
public:
    virtual ~IEngine() {}

    // True while the space simulation exists and the player's ship is reachable.
    virtual bool isInSpaceStage() = 0;

    // Current reference frame of the local player (galaxy / system / planet).
    virtual bool getFrame(Frame& out) = 0;

    // Local player's ship identity and pose (in the current frame).
    virtual bool getLocalShip(LocalShipInfo& info, Pose& pose) = 0;

    // Create a ghost for a remote entity. Returns an opaque engine handle or null.
    virtual void* spawnGhost(const EntitySpawn& spec, uint32_t localPoliticalId, std::string& err) = 0;

    // Move an existing ghost towards the given pose.
    virtual void driveGhost(void* handle, const Pose& target, GhostDriveMode mode, float dtSec) = 0;

    // Read back where the engine actually put the ghost (for diagnostics / tests).
    virtual bool getGhostPose(void* handle, Pose& out) = 0;

    // The engine can collect objects on its own; poll this before touching a handle.
    virtual bool isGhostAlive(void* handle) = 0;

    virtual void destroyGhost(void* handle) = 0;

    virtual uint32_t nowMs() = 0;
    virtual void log(const std::string& line) = 0;
};

} // namespace osmp
