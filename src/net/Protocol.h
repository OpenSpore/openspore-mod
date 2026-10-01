// Protocol.h - OpenSpore multiplayer wire protocol (M2).
//
// Design decisions (see idapro-reverse-dump/docs/CONTEXT.md):
//  * Entities are identified by our own NetId, never by engine pointers (the engine
//    recreates objects, e.g. the ally UFO changed address within one session).
//  * A position is meaningless without its frame of reference. Spore switches between
//    galaxy / solar-system / planet-local coordinates, and every large coordinate jump
//    coincided with cSimulatorSpaceGame::mpHighLODPlanetSim becoming null/non-null.
//    Therefore every transform carries a Frame {kind, starId, planetId}.
//  * The engine is non-deterministic (one global LCG consumed by 353 functions), so
//    the protocol replicates results, never seeds.
//  * Control messages are reliable (ack + resend), snapshots are unreliable and
//    sequence-numbered so stale ones are dropped.
#pragma once

#include "ByteStream.h"
#include <cstdint>
#include <string>
#include <vector>

namespace osmp {

constexpr uint32_t kMagic = 0x504D534Fu;      // "OSMP" as little-endian bytes
constexpr uint8_t  kProtocolVersion = 1;
constexpr size_t   kHeaderSize = 12;
constexpr size_t   kMaxPacketSize = 1200;      // stays under the common MTU

enum class MsgType : uint8_t {
    Invalid = 0,
    // session control (reliable)
    Hello = 1,
    Welcome = 2,
    Reject = 3,
    Bye = 4,
    // keepalive (unreliable)
    Ping = 5,
    Pong = 6,
    // transport internal
    Ack = 7,
    // entity control (reliable)
    EntitySpawn = 16,
    EntityDespawn = 17,
    FrameChange = 18,
    // replication (unreliable)
    Transform = 32,
};

enum PacketFlags : uint8_t {
    kFlagReliable = 0x01,
};

enum class FrameKind : uint8_t {
    None = 0,     // not in space stage / unknown
    Galaxy = 1,   // galaxy view: no planet sim, star pointer may be null
    System = 2,   // inside a solar system, no high-LOD planet
    Planet = 3,   // near a planet: cSimulatorSpaceGame::mpHighLODPlanetSim != null
};

enum class EntityKind : uint8_t {
    None = 0,
    UFO = 1,
};

enum class RejectReason : uint8_t {
    None = 0,
    VersionMismatch = 1,
    SessionFull = 2,
    NotInSpaceStage = 3,
    BuildMismatch = 4,
};

struct Frame {
    FrameKind kind = FrameKind::None;
    uint32_t starId = 0;    // Simulator::StarID::internalValue (0 = unknown)
    uint32_t planetId = 0;  // Simulator::PlanetID::internalValue (0 = none)

    bool operator==(const Frame& o) const { return kind == o.kind && starId == o.starId && planetId == o.planetId; }
    bool operator!=(const Frame& o) const { return !(*this == o); }
};

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };

struct ResKey { uint32_t instanceId = 0, typeId = 0, groupId = 0; };

struct PacketHeader {
    uint32_t magic = kMagic;
    uint8_t version = kProtocolVersion;
    MsgType type = MsgType::Invalid;
    uint8_t flags = 0;
    uint8_t reserved = 0;
    uint16_t seq = 0;   // reliable: message id; unreliable: sequence number; Ack: acked id
    uint16_t ack = 0;   // highest reliable id received so far (informational)
};

// ---- message payloads ---------------------------------------------------

struct Hello {
    uint8_t protocolVersion = kProtocolVersion;
    uint32_t gameBuild = 0;     // e.g. 3010029 for 3.1.0.29
    uint32_t politicalId = 0;   // player's empire political ID
    std::string playerName;
};

struct Welcome {
    uint8_t playerId = 0;       // 1 = host, 2 = client
    uint32_t sessionId = 0;
    uint8_t snapshotHz = 15;
    uint32_t hostPoliticalId = 0;
    std::string hostName;
};

struct Reject { RejectReason reason = RejectReason::None; };

struct Ping { uint32_t timeMs = 0; };
struct Pong { uint32_t timeMs = 0; };

struct EntitySpawn {
    uint32_t netId = 0;
    uint8_t ownerPlayerId = 0;
    EntityKind kind = EntityKind::UFO;
    uint8_t ufoType = 0;        // Simulator::UfoType
    uint32_t politicalId = 0;
    ResKey model;               // UFO model key (cEmpire::mUFOKey of the owner)
    Frame frame;
    Vec3 pos;
    Quat rot;
};

struct EntityDespawn { uint32_t netId = 0; };

struct FrameChange {
    uint8_t playerId = 0;
    Frame frame;
};

struct Transform {
    uint32_t netId = 0;
    uint32_t tick = 0;      // sender's simulation tick / ms timestamp
    Frame frame;
    Vec3 pos;
    Quat rot;
    Vec3 vel;
};

// ---- header -------------------------------------------------------------

void writeHeader(ByteWriter& w, const PacketHeader& h);
bool readHeader(ByteReader& r, PacketHeader& h);   // validates magic + version

// ---- payload encode / decode -------------------------------------------

void encode(ByteWriter& w, const Frame& v);
bool decode(ByteReader& r, Frame& v);
void encode(ByteWriter& w, const Vec3& v);
bool decode(ByteReader& r, Vec3& v);
void encode(ByteWriter& w, const Quat& v);
bool decode(ByteReader& r, Quat& v);
void encode(ByteWriter& w, const ResKey& v);
bool decode(ByteReader& r, ResKey& v);

void encode(ByteWriter& w, const Hello& m);
bool decode(ByteReader& r, Hello& m);
void encode(ByteWriter& w, const Welcome& m);
bool decode(ByteReader& r, Welcome& m);
void encode(ByteWriter& w, const Reject& m);
bool decode(ByteReader& r, Reject& m);
void encode(ByteWriter& w, const Ping& m);
bool decode(ByteReader& r, Ping& m);
void encode(ByteWriter& w, const Pong& m);
bool decode(ByteReader& r, Pong& m);
void encode(ByteWriter& w, const EntitySpawn& m);
bool decode(ByteReader& r, EntitySpawn& m);
void encode(ByteWriter& w, const EntityDespawn& m);
bool decode(ByteReader& r, EntityDespawn& m);
void encode(ByteWriter& w, const FrameChange& m);
bool decode(ByteReader& r, FrameChange& m);
void encode(ByteWriter& w, const Transform& m);
bool decode(ByteReader& r, Transform& m);

template <typename T>
std::vector<uint8_t> toBytes(const T& m) {
    ByteWriter w;
    encode(w, m);
    return w.data();
}

const char* msgTypeName(MsgType t);
const char* frameKindName(FrameKind k);
bool isReliableType(MsgType t);

} // namespace osmp
