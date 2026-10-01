#include "Interpolation.h"
#include <cmath>

namespace osmp {

float length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    Vec3 r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;
    return r;
}

Quat normalize(const Quat& q) {
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n < 1e-6f) { Quat id; return id; }
    Quat r; r.x = q.x / n; r.y = q.y / n; r.z = q.z / n; r.w = q.w / n;
    return r;
}

Quat slerp(const Quat& a0, const Quat& b0, float t) {
    Quat a = normalize(a0), b = normalize(b0);
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; dot = -dot; }
    if (dot > 0.9995f) {
        Quat r; r.x = a.x + (b.x - a.x) * t; r.y = a.y + (b.y - a.y) * t; r.z = a.z + (b.z - a.z) * t; r.w = a.w + (b.w - a.w) * t;
        return normalize(r);
    }
    float theta0 = std::acos(dot);
    float theta = theta0 * t;
    float sinTheta = std::sin(theta), sinTheta0 = std::sin(theta0);
    float s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
    float s1 = sinTheta / sinTheta0;
    Quat r; r.x = s0 * a.x + s1 * b.x; r.y = s0 * a.y + s1 * b.y; r.z = s0 * a.z + s1 * b.z; r.w = s0 * a.w + s1 * b.w;
    return normalize(r);
}

bool InterpBuffer::push(uint32_t timeMs, const Vec3& pos, const Quat& rot, const Vec3& vel) {
    if (!snaps_.empty() && (int32_t)(timeMs - snaps_.back().timeMs) <= 0) return false;
    Snapshot s; s.timeMs = timeMs; s.pos = pos; s.rot = normalize(rot); s.vel = vel;
    snaps_.push_back(s);
    while (snaps_.size() > capacity_) snaps_.pop_front();
    return true;
}

bool InterpBuffer::sample(uint32_t renderTimeMs, Vec3& pos, Quat& rot, uint32_t maxExtrapolateMs) const {
    if (snaps_.empty()) return false;
    const Snapshot& first = snaps_.front();
    const Snapshot& last = snaps_.back();
    if ((int32_t)(renderTimeMs - first.timeMs) <= 0) { pos = first.pos; rot = first.rot; return true; }
    if ((int32_t)(renderTimeMs - last.timeMs) >= 0) {
        uint32_t ahead = renderTimeMs - last.timeMs;
        if (ahead > maxExtrapolateMs) ahead = maxExtrapolateMs;
        float dt = ahead / 1000.0f;
        pos.x = last.pos.x + last.vel.x * dt;
        pos.y = last.pos.y + last.vel.y * dt;
        pos.z = last.pos.z + last.vel.z * dt;
        rot = last.rot;
        return true;
    }
    for (size_t i = 0; i + 1 < snaps_.size(); ++i) {
        const Snapshot& a = snaps_[i];
        const Snapshot& b = snaps_[i + 1];
        if ((int32_t)(renderTimeMs - a.timeMs) >= 0 && (int32_t)(b.timeMs - renderTimeMs) >= 0) {
            uint32_t span = b.timeMs - a.timeMs;
            float t = span ? (float)(renderTimeMs - a.timeMs) / (float)span : 1.0f;
            pos = lerp(a.pos, b.pos, t);
            rot = slerp(a.rot, b.rot, t);
            return true;
        }
    }
    pos = last.pos; rot = last.rot;
    return true;
}

} // namespace osmp
