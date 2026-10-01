#include "test_framework.h"
#include "net/Interpolation.h"
#include "net/EntityRegistry.h"
using namespace osmp;

TEST(interp_linear_motion) {
    InterpBuffer b;
    for (uint32_t t = 0; t <= 1000; t += 100) { Vec3 p{(float)t, 0, 0}; Quat q{0, 0, 0, 1}; Vec3 v{1000, 0, 0}; CHECK(b.push(t, p, q, v)); }
    Vec3 p; Quat q;
    CHECK(b.sample(250, p, q)); CHECK_NEAR(p.x, 250.0f, 1e-3);
    CHECK(b.sample(0, p, q)); CHECK_NEAR(p.x, 0.0f, 1e-3);
    CHECK(b.sample(1000, p, q)); CHECK_NEAR(p.x, 1000.0f, 1e-3);
    // extrapolation is clamped at 250 ms
    CHECK(b.sample(1100, p, q)); CHECK_NEAR(p.x, 1100.0f, 1e-3);
    CHECK(b.sample(5000, p, q)); CHECK_NEAR(p.x, 1250.0f, 1e-3);
}

TEST(interp_rejects_out_of_order) {
    InterpBuffer b; Vec3 p{0,0,0}; Quat q; Vec3 v;
    CHECK(b.push(100, p, q, v)); CHECK(!b.push(100, p, q, v)); CHECK(!b.push(50, p, q, v)); CHECK(b.push(101, p, q, v));
    CHECK_EQ(b.size(), 2u);
}

TEST(interp_capacity) {
    InterpBuffer b(4); Vec3 p; Quat q; Vec3 v;
    for (uint32_t t = 1; t <= 10; ++t) b.push(t, p, q, v);
    CHECK_EQ(b.size(), 4u); CHECK_EQ(b.latestTimeMs(), 10u);
}

TEST(slerp_is_unit_and_monotonic) {
    Quat a{0, 0, 0, 1};
    Quat bq{0, 0, 0.7071068f, 0.7071068f};   // 90 degrees about Z
    Quat half = slerp(a, bq, 0.5f);
    float n = std::sqrt(half.x*half.x + half.y*half.y + half.z*half.z + half.w*half.w);
    CHECK_NEAR(n, 1.0f, 1e-5);
    CHECK_NEAR(half.z, std::sin(3.14159265f / 8), 1e-4);   // 45 degrees about Z
    CHECK_NEAR(half.w, std::cos(3.14159265f / 8), 1e-4);
    Quat s0 = slerp(a, bq, 0.0f); CHECK_NEAR(s0.w, 1.0f, 1e-5);
    Quat s1 = slerp(a, bq, 1.0f); CHECK_NEAR(s1.z, 0.7071068f, 1e-5);
}

TEST(registry_netid_layout) {
    EntityRegistry reg;
    uint32_t id1 = reg.allocate(1), id2 = reg.allocate(2);
    CHECK_EQ(EntityRegistry::ownerOf(id1), 1); CHECK_EQ(EntityRegistry::ownerOf(id2), 2); CHECK(id1 != id2);
    EntitySpawn s; s.netId = id2; s.ownerPlayerId = 2; s.frame.kind = FrameKind::System; s.frame.starId = 5;
    reg.add(s); CHECK_EQ(reg.size(), 1u); CHECK(reg.find(id2) != nullptr); CHECK(reg.find(id1) == nullptr);
    Transform t; t.netId = id2; t.tick = 10; t.frame = s.frame; t.pos = {1, 1, 1};
    CHECK(reg.applyTransform(t, 10)); CHECK_EQ(reg.find(id2)->interp.size(), 1u);
    t.tick = 5; CHECK(!reg.applyTransform(t, 11));                // stale tick dropped
    t.tick = 20; t.frame.kind = FrameKind::Planet; t.frame.planetId = 77;
    CHECK(reg.applyTransform(t, 12)); CHECK_EQ(reg.find(id2)->interp.size(), 1u);  // frame change clears samples
    CHECK(reg.find(id2)->frame.kind == FrameKind::Planet);
    CHECK(reg.remove(id2)); CHECK_EQ(reg.size(), 0u);
}
