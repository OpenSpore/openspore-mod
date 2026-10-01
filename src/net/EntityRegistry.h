// EntityRegistry.h - NetId <-> replicated entity bookkeeping (portable part).
//
// NetId layout: (ownerPlayerId << 24) | counter. The owner allocates ids for the
// entities it is authoritative over, so no round trip is needed.
#pragma once

#include "Interpolation.h"
#include "Protocol.h"

#include <map>

namespace osmp {

struct NetEntity {
    uint32_t netId = 0;
    uint8_t ownerPlayerId = 0;
    EntityKind kind = EntityKind::None;
    uint8_t ufoType = 0;
    uint32_t politicalId = 0;
    ResKey model;
    Frame frame;             // frame of the latest transform
    Transform last;          // latest transform received or sampled
    InterpBuffer interp;     // remote entities only
    void* handle = nullptr;  // engine object (cGameDataUFO*), owned by the game layer
    bool spawned = false;    // engine object exists
    uint32_t lastUpdateMs = 0;
};

class EntityRegistry {
public:
    static uint8_t ownerOf(uint32_t netId) { return (uint8_t)(netId >> 24); }

    uint32_t allocate(uint8_t ownerPlayerId) {
        uint32_t id = ((uint32_t)ownerPlayerId << 24) | (nextCounter_++ & 0x00FFFFFFu);
        if ((nextCounter_ & 0x00FFFFFFu) == 0) nextCounter_ = 1;
        return id;
    }

    NetEntity& add(const EntitySpawn& s) {
        NetEntity& e = entities_[s.netId];
        e.netId = s.netId;
        e.ownerPlayerId = s.ownerPlayerId;
        e.kind = s.kind;
        e.ufoType = s.ufoType;
        e.politicalId = s.politicalId;
        e.model = s.model;
        e.frame = s.frame;
        e.last.netId = s.netId;
        e.last.frame = s.frame;
        e.last.pos = s.pos;
        e.last.rot = s.rot;
        return e;
    }

    bool remove(uint32_t netId) { return entities_.erase(netId) > 0; }
    NetEntity* find(uint32_t netId) {
        auto it = entities_.find(netId);
        return it == entities_.end() ? nullptr : &it->second;
    }
    std::map<uint32_t, NetEntity>& all() { return entities_; }
    const std::map<uint32_t, NetEntity>& all() const { return entities_; }
    size_t size() const { return entities_.size(); }
    void clear() { entities_.clear(); }

    // Apply an incoming transform; returns false if the entity is unknown.
    bool applyTransform(const Transform& t, uint32_t nowMs) {
        NetEntity* e = find(t.netId);
        if (!e) return false;
        if (e->last.tick != 0 && (int32_t)(t.tick - e->last.tick) <= 0) return false;  // stale
        if (t.frame != e->frame) {
            e->frame = t.frame;       // frame change: old samples are in another coordinate space
            e->interp.clear();
        }
        e->last = t;
        e->lastUpdateMs = nowMs;
        e->interp.push(t.tick, t.pos, t.rot, t.vel);
        return true;
    }

private:
    std::map<uint32_t, NetEntity> entities_;
    uint32_t nextCounter_ = 1;
};

} // namespace osmp
