#include "test_framework.h"
#include "FakeEngine.h"
#include "game/Replication.h"
#include "net/Session.h"
using namespace osmp;

namespace {
struct World {
    FakeEngine engA, engB;
    Session sesA, sesB;
    Replication repA, repB;
    uint32_t now = 10000;

    World(Replication::Config cfg = Replication::Config()) : repA(engA, sesA, cfg), repB(engB, sesB, cfg) {
        wire(sesA, repA); wire(sesB, repB);
        engB.ship.politicalId = 0x8000157B;
    }
    static void wire(Session& s, Replication& r) {
        s.setCallbacks([&r](const Session::PeerInfo&) { r.onConnected(); }, [&r](const std::string&) { r.onDisconnected(); },
                       [&r](MsgType t, ByteReader& p) { r.onMessage(t, p); });
    }
    bool connect() {
        if (!sesA.host(0)) return false;
        sesA.update(now);
        if (!sesB.join(Endpoint::loopback(sesA.localPort()))) return false;
        for (int i = 0; i < 100 && !(sesA.isConnected() && sesB.isConnected()); ++i) tick(1);
        return sesA.isConnected() && sesB.isConnected();
    }
    // one simulation tick on both machines (~33 ms)
    void tick(int n = 1, uint32_t stepMs = 33) {
        for (int i = 0; i < n; ++i) {
            now += stepMs; engA.clock = now; engB.clock = now;
            sesA.update(now); sesB.update(now);
            repA.update(); repB.update();
        }
    }
};
}

TEST(replication_spawns_ghost_on_both_sides_in_same_frame) {
    World w; CHECK(w.connect());
    w.tick(10);
    CHECK_EQ(w.engA.aliveGhosts(), 1u); CHECK_EQ(w.engB.aliveGhosts(), 1u);
    CHECK(w.repA.remoteGhostVisible()); CHECK(w.repB.remoteGhostVisible());
    CHECK_EQ(w.repA.stats().spawnsSent, 1u); CHECK_EQ(w.repA.stats().spawnsRecv, 1u);
    // the ghost on B carries A's identity
    for (auto& kv : w.engB.ghosts) { CHECK_EQ(kv.second.spec.politicalId, 0x800021CAu); CHECK_EQ(kv.second.spec.model.groupId, 3u); CHECK_EQ(EntityRegistry::ownerOf(kv.second.spec.netId), 1); }
}

TEST(replication_ghost_follows_remote_ship) {
    World w; CHECK(w.connect());
    w.tick(10);
    // A flies along +X at 100 units/s for 2 seconds
    for (int i = 0; i < 60; ++i) { w.engA.shipPose.pos.x += 100.0f * 0.033f; w.engA.shipPose.vel = {100, 0, 0}; w.tick(1); }
    CHECK(w.repB.stats().snapshotsRecv >= 20);
    Pose gp; bool found = false;
    for (auto& kv : w.engB.ghosts) { found = w.engB.getGhostPose(kv.first, gp); CHECK(kv.second.drives > 10); }
    CHECK(found);
    // ghost trails the real ship by about the render delay (2 snapshots = 133 ms = ~13 units), never leads it
    float lag = w.engA.shipPose.pos.x - gp.pos.x;
    CHECK(lag >= 0.0f); CHECK(lag < 40.0f);
}

TEST(replication_frame_change_hides_and_restores_ghost) {
    World w; CHECK(w.connect());
    w.tick(10);
    CHECK_EQ(w.engB.aliveGhosts(), 1u);
    // A dives to a planet: different frame -> B must remove the ghost (coordinates are meaningless there)
    w.engA.frame.kind = FrameKind::Planet; w.engA.frame.planetId = 0x05001234;
    w.tick(10);
    CHECK_EQ(w.engB.aliveGhosts(), 0u);
    CHECK(w.repA.stats().frameChangesSent >= 1); CHECK(w.repB.stats().frameChangesRecv >= 1);
    CHECK_EQ(w.engA.aliveGhosts(), 0u);   // and A no longer shows B either
    // B follows to the same planet -> ghosts come back on both sides
    w.engB.frame = w.engA.frame;
    w.tick(10);
    CHECK_EQ(w.engB.aliveGhosts(), 1u); CHECK_EQ(w.engA.aliveGhosts(), 1u);
}

TEST(replication_respawns_when_engine_collects_ghost) {
    World w; CHECK(w.connect());
    w.tick(10);
    for (auto& kv : w.engB.ghosts) kv.second.alive = false;   // engine collected it
    w.tick(40);                                               // > ghostAliveCheckMs
    CHECK_EQ(w.engB.aliveGhosts(), 1u);
    CHECK_EQ(w.repB.stats().ghostsLostByEngine, 1u);
}

TEST(replication_disconnect_cleans_up_and_spawn_failure_is_counted) {
    Replication::Config cfg; World w(cfg); CHECK(w.connect());
    w.tick(10);
    w.sesB.leave();
    w.tick(5);
    CHECK_EQ(w.engA.aliveGhosts(), 0u); CHECK_EQ(w.repA.registry().size(), 0u);

    World v; v.engB.failSpawns = true; CHECK(v.connect());
    v.tick(10);
    CHECK_EQ(v.engB.aliveGhosts(), 0u); CHECK(v.repB.stats().ghostSpawnFailures >= 1);
    CHECK_EQ(v.engA.aliveGhosts(), 1u);
}

TEST(replication_leaving_space_stage_removes_ghosts_and_mode_switch) {
    World w; CHECK(w.connect());
    w.tick(10);
    w.engB.inSpace = false;
    w.tick(5);
    CHECK_EQ(w.engB.aliveGhosts(), 0u);
    w.engB.inSpace = true;
    w.repB.setDriveMode(GhostDriveMode::Teleport);
    w.tick(10);
    CHECK_EQ(w.engB.aliveGhosts(), 1u);
    for (auto& kv : w.engB.ghosts) CHECK(kv.second.lastMode == GhostDriveMode::Teleport);
    GhostDriveMode m; CHECK(parseGhostDriveMode("velocity", m)); CHECK(m == GhostDriveMode::Velocity); CHECK(!parseGhostDriveMode("warp", m));
    CHECK(!w.repA.statusLine().empty());
}
