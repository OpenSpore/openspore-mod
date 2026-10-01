#include "Protocol.h"

namespace osmp {

void writeHeader(ByteWriter& w, const PacketHeader& h) {
    w.u32(h.magic);
    w.u8(h.version);
    w.u8((uint8_t)h.type);
    w.u8(h.flags);
    w.u8(h.reserved);
    w.u16(h.seq);
    w.u16(h.ack);
}

bool readHeader(ByteReader& r, PacketHeader& h) {
    if (r.remaining() < kHeaderSize) return false;
    h.magic = r.u32();
    h.version = r.u8();
    h.type = (MsgType)r.u8();
    h.flags = r.u8();
    h.reserved = r.u8();
    h.seq = r.u16();
    h.ack = r.u16();
    if (!r.ok()) return false;
    if (h.magic != kMagic) return false;
    if (h.version != kProtocolVersion) return false;
    if (h.type == MsgType::Invalid) return false;
    return true;
}

void encode(ByteWriter& w, const Frame& v) { w.u8((uint8_t)v.kind); w.u32(v.starId); w.u32(v.planetId); }
bool decode(ByteReader& r, Frame& v) {
    v.kind = (FrameKind)r.u8(); v.starId = r.u32(); v.planetId = r.u32();
    return r.ok() && (uint8_t)v.kind <= (uint8_t)FrameKind::Planet;
}
void encode(ByteWriter& w, const Vec3& v) { w.f32(v.x); w.f32(v.y); w.f32(v.z); }
bool decode(ByteReader& r, Vec3& v) { v.x = r.f32(); v.y = r.f32(); v.z = r.f32(); return r.ok(); }
void encode(ByteWriter& w, const Quat& v) { w.f32(v.x); w.f32(v.y); w.f32(v.z); w.f32(v.w); }
bool decode(ByteReader& r, Quat& v) { v.x = r.f32(); v.y = r.f32(); v.z = r.f32(); v.w = r.f32(); return r.ok(); }
void encode(ByteWriter& w, const ResKey& v) { w.u32(v.instanceId); w.u32(v.typeId); w.u32(v.groupId); }
bool decode(ByteReader& r, ResKey& v) { v.instanceId = r.u32(); v.typeId = r.u32(); v.groupId = r.u32(); return r.ok(); }

void encode(ByteWriter& w, const Hello& m) {
    w.u8(m.protocolVersion); w.u32(m.gameBuild); w.u32(m.politicalId); w.str(m.playerName);
}
bool decode(ByteReader& r, Hello& m) {
    m.protocolVersion = r.u8(); m.gameBuild = r.u32(); m.politicalId = r.u32(); m.playerName = r.str();
    return r.ok();
}

void encode(ByteWriter& w, const Welcome& m) {
    w.u8(m.playerId); w.u32(m.sessionId); w.u8(m.snapshotHz); w.u32(m.hostPoliticalId); w.str(m.hostName);
}
bool decode(ByteReader& r, Welcome& m) {
    m.playerId = r.u8(); m.sessionId = r.u32(); m.snapshotHz = r.u8(); m.hostPoliticalId = r.u32(); m.hostName = r.str();
    return r.ok();
}

void encode(ByteWriter& w, const Reject& m) { w.u8((uint8_t)m.reason); }
bool decode(ByteReader& r, Reject& m) { m.reason = (RejectReason)r.u8(); return r.ok(); }

void encode(ByteWriter& w, const Ping& m) { w.u32(m.timeMs); }
bool decode(ByteReader& r, Ping& m) { m.timeMs = r.u32(); return r.ok(); }
void encode(ByteWriter& w, const Pong& m) { w.u32(m.timeMs); }
bool decode(ByteReader& r, Pong& m) { m.timeMs = r.u32(); return r.ok(); }

void encode(ByteWriter& w, const EntitySpawn& m) {
    w.u32(m.netId); w.u8(m.ownerPlayerId); w.u8((uint8_t)m.kind); w.u8(m.ufoType); w.u32(m.politicalId);
    encode(w, m.model); encode(w, m.frame); encode(w, m.pos); encode(w, m.rot);
}
bool decode(ByteReader& r, EntitySpawn& m) {
    m.netId = r.u32(); m.ownerPlayerId = r.u8(); m.kind = (EntityKind)r.u8(); m.ufoType = r.u8(); m.politicalId = r.u32();
    return decode(r, m.model) && decode(r, m.frame) && decode(r, m.pos) && decode(r, m.rot) && r.ok();
}

void encode(ByteWriter& w, const EntityDespawn& m) { w.u32(m.netId); }
bool decode(ByteReader& r, EntityDespawn& m) { m.netId = r.u32(); return r.ok(); }

void encode(ByteWriter& w, const FrameChange& m) { w.u8(m.playerId); encode(w, m.frame); }
bool decode(ByteReader& r, FrameChange& m) { m.playerId = r.u8(); return decode(r, m.frame) && r.ok(); }

void encode(ByteWriter& w, const Transform& m) {
    w.u32(m.netId); w.u32(m.tick); encode(w, m.frame); encode(w, m.pos); encode(w, m.rot); encode(w, m.vel);
}
bool decode(ByteReader& r, Transform& m) {
    m.netId = r.u32(); m.tick = r.u32();
    return decode(r, m.frame) && decode(r, m.pos) && decode(r, m.rot) && decode(r, m.vel) && r.ok();
}

const char* msgTypeName(MsgType t) {
    switch (t) {
    case MsgType::Hello: return "Hello";
    case MsgType::Welcome: return "Welcome";
    case MsgType::Reject: return "Reject";
    case MsgType::Bye: return "Bye";
    case MsgType::Ping: return "Ping";
    case MsgType::Pong: return "Pong";
    case MsgType::Ack: return "Ack";
    case MsgType::EntitySpawn: return "EntitySpawn";
    case MsgType::EntityDespawn: return "EntityDespawn";
    case MsgType::FrameChange: return "FrameChange";
    case MsgType::Transform: return "Transform";
    default: return "Invalid";
    }
}

const char* frameKindName(FrameKind k) {
    switch (k) {
    case FrameKind::Galaxy: return "GALAXY";
    case FrameKind::System: return "SYSTEM";
    case FrameKind::Planet: return "PLANET";
    default: return "NONE";
    }
}

bool isReliableType(MsgType t) {
    switch (t) {
    case MsgType::Hello: case MsgType::Welcome: case MsgType::Reject: case MsgType::Bye:
    case MsgType::EntitySpawn: case MsgType::EntityDespawn: case MsgType::FrameChange:
        return true;
    default:
        return false;
    }
}

} // namespace osmp
