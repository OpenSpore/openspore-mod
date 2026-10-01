// Interpolation.h - snapshot buffer for remote entities: render the ghost a little
// in the past and interpolate between the two snapshots around the render time.
// First movement pass is deliberately dumb (no prediction/reconciliation).
#pragma once

#include "Protocol.h"
#include <deque>

namespace osmp {

Vec3 lerp(const Vec3& a, const Vec3& b, float t);
Quat normalize(const Quat& q);
Quat slerp(const Quat& a, const Quat& b, float t);
float length(const Vec3& v);

class InterpBuffer {
public:
    struct Snapshot {
        uint32_t timeMs = 0;
        Vec3 pos;
        Quat rot;
        Vec3 vel;
    };

    explicit InterpBuffer(size_t capacity = 32) : capacity_(capacity) {}

    // Snapshots must arrive in increasing time; older ones are ignored.
    bool push(uint32_t timeMs, const Vec3& pos, const Quat& rot, const Vec3& vel);
    void clear() { snaps_.clear(); }
    size_t size() const { return snaps_.size(); }
    bool empty() const { return snaps_.empty(); }
    uint32_t latestTimeMs() const { return snaps_.empty() ? 0 : snaps_.back().timeMs; }
    const Snapshot* latest() const { return snaps_.empty() ? nullptr : &snaps_.back(); }

    // Sample the pose at renderTimeMs. Extrapolates with velocity for at most
    // maxExtrapolateMs beyond the newest snapshot, then holds the last pose.
    bool sample(uint32_t renderTimeMs, Vec3& pos, Quat& rot, uint32_t maxExtrapolateMs = 250) const;

private:
    size_t capacity_;
    std::deque<Snapshot> snaps_;
};

} // namespace osmp
