// RawAddresses.h - Spore: Galactic Adventures, Steam/GOG "march2017" family (3.1.0.22 / 3.1.0.29).
//
// All values are relative to the executable's preferred ImageBase 0x400000 (the convention
// of the ModAPI address tables, second value of SelectAddress). The lab translates them to
// the live base at runtime. idapro-reverse-dump addresses (base 0xB00000) are given minus 0x700000.
//
// Sources: Spore ModAPI SourceCode/DLL/AddressesSimulator.cpp, idapro-reverse-dump/docs/CONTEXT.md.
#pragma once

#include <cstdint>

namespace osmp { namespace raw {

constexpr uintptr_t kPreferredBase = 0x400000;

// --- self-test: tiny function `mov eax, "cSimulatorSpaceGame"` (CONTEXT.md kGetClassName 0x125C410)
constexpr uintptr_t kGetClassNameFn = 0x0B5C410;

// --- simulator singletons (each subsystem stores `this` in a global in its ctor)
constexpr uintptr_t kSpaceGameSingleton = 0x16DC0FC;   // cSimulatorSpaceGame*  (IDA 0x1DDC0FC)
constexpr uintptr_t kPlayerUfoSingleton = 0x16DBA14;   // cSimulatorPlayerUFO*  (IDA 0x1DDBA14)
constexpr uintptr_t kSpaceGameVTable = 0x1495454;      // primary vtable, slot 4 = Update   (IDA 0x1B95454)
constexpr int       kSpaceGameUpdateSlot = 4;
constexpr uintptr_t kSpacePlayerDataPtr = 0x16DDA8C;   // SpacePlayerData* global (ModAPI sSpacePlayerData, IDA 0x1DDDA8C)

// --- free functions (cdecl)
constexpr uintptr_t kGetActiveStar = 0x10210E0;        // cStar*
constexpr uintptr_t kGetActiveStarRecord = 0x10210F0;  // cStarRecord*
constexpr uintptr_t kGetCurrentContext = 0x1020F30;    // SpaceContext (int): -1 none, 0 planet, 1 system, 2 galaxy
constexpr uintptr_t kGetPlayerEmpire = 0x10211B0;      // cEmpire*
constexpr uintptr_t kGetPlayerEmpireID = 0x1020F40;    // uint32_t
constexpr uintptr_t kSpawnUFO = 0x102ACC0;             // cGameDataUFO* SpawnUFO(UfoType, const uint32_t& politicalID, const ResourceKey& modelKey)
constexpr uintptr_t kCreateUFO = 0x102AC60;            // cGameDataUFO* CreateUFO(UfoType, cEmpire*)

// --- cGameNounManager (thiscall members)
constexpr uintptr_t kNounManagerGet = 0xB3D400;        // cGameNounManager* Get()
constexpr uintptr_t kNounCreateInstance = 0xB20BF0;    // cGameData* CreateInstance(uint32_t nounID)
constexpr uintptr_t kNounDestroyInstance = 0xB22560;   // void DestroyInstance(cGameData*)
constexpr uintptr_t kUfoInitialize = 0xC3E210;         // void cGameDataUFO::Initialize(UfoType, cEmpire*)
constexpr uint32_t  kNounGameDataUFO = 0x18EBADC;

// --- object layouts
namespace GameData {                 // cGameData (0x34)
    constexpr int mpView = 0x14;
    constexpr int mID = 0x24;
    constexpr int mPoliticalID = 0x30;
    constexpr int refCount = 0x08;
}
namespace Ufo {                      // cGameDataUFO (0x818): cGameData 0, cLocomotiveObject 0x34, cCombatant 0x508, cBehaviorList 0x5D0
    constexpr int locomotiveBase = 0x34;
    constexpr int position = 0x38;          // cSpatialObject::mPosition (derived, do not write)
    constexpr int orientation = 0x44;       // cSpatialObject::mOrientation
    constexpr int modelKey = 0x34 + 0x90;   // cSpatialObject::mModelKey (ResourceKey)
    constexpr int velocity = 0x34 + 0x1C8;  // cLocomotiveObject::mVelocity
    constexpr int desiredSpeed = 0x34 + 0x1E0;
    constexpr int mNPCFollowUFO = 0x68C;
    constexpr int mNPCFlockIndex = 0x6B0;
    constexpr int mUFOType = 0x714;
    constexpr int mNextPosition = 0x718;
    constexpr int mNextVelocity = 0x724;
    constexpr int mNextOrientation = 0x730;
    constexpr int mbAtDestination = 0x74C;
    constexpr int mDestination = 0x750;
    constexpr int size = 0x818;
}
namespace SpatialVt {                // cSpatialObject vtable (at obj + 0x34)
    constexpr int SetPosition = 0x38;
    constexpr int SetOrientation = 0x3C;
    constexpr int Teleport = 0x44;
}
namespace LocoVt {                   // cLocomotiveObject vtable (same vtable pointer as cSpatialObject sub-object)
    constexpr int SetDesiredSpeed = 0xC4;
    constexpr int MoveTo = 0xE0;
    constexpr int StopMovement = 0xEC;
}
namespace SpaceGame {                // cSimulatorSpaceGame (0xF0)
    constexpr int mpHighLODPlanetSim = 0x54;
    constexpr int mNPC_UFOs = 0x78;         // eastl::vector<cGameDataUFOPtr>
    constexpr int mpPlayerUFO = 0xB4;       // cSimulatorPlayerUFO*
}
namespace PlayerUfoSim {             // cSimulatorPlayerUFO (0xE0)
    constexpr int mpPlayerUFO = 0x40;       // cGameDataUFO*
}
namespace SpacePlayerData {
    constexpr int mpActivePlanet = 0x04;    // cPlanet*
    constexpr int mpActiveStar = 0x08;      // cStar*
    constexpr int mCurrentContext = 0x10;   // SpaceContext
    constexpr int mPlayerEmpireID = 0x18;
}
namespace Star {                     // cStar
    constexpr int mpStarRecord = 0x48;
    constexpr int mKey = 0x4C;              // StarID
}
namespace StarRecord {
    constexpr int mKey = 0x70;              // StarID
}
namespace PlanetRecord {
    constexpr int mKey = 0x184;             // PlanetID
}
namespace Planet {                   // cPlanet
    constexpr int mpPlanetRecord = 0x13C;   // cPlanetRecordPtr
    constexpr int mPlanetKey = 0x140;       // uint32_t, PlanetID value
}
namespace NounManager {
    constexpr int mNouns = 0x78;            // eastl::intrusive_list<cGameData>, node = object + 0x0C
    constexpr int nodeToObject = 0x0C;
}

}} // namespace osmp::raw
