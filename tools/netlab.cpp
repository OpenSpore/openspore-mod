// netlab - exercise the OpenSpore transport between two machines without the game.
//
//   osmp_netlab host <port>
//   osmp_netlab join <ip> <port>
//
// Both sides spawn one fake entity and stream a circular trajectory at 15 Hz; the
// peer prints what it receives. Use it to validate port forwarding / VPN / firewall
// before trying the real mod.
#include "net/EntityRegistry.h"
#include "net/Session.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace osmp;

static uint32_t nowMs() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: %s host <port> | join <ip> <port>\n", argv[0]); return 2; }

    Session::Config cfg;
    cfg.playerName = argc > 4 ? argv[4] : (std::strcmp(argv[1], "host") == 0 ? "host" : "client");
    cfg.gameBuild = 3010029;
    Session s(cfg);
    s.setLogger([](const std::string& l) { std::printf("[session] %s\n", l.c_str()); });

    EntityRegistry reg;
    uint32_t myEntity = 0;
    bool spawnSent = false;

    s.setCallbacks(
        [&](const Session::PeerInfo& p) { std::printf("connected: player %d '%s' at %s\n", p.playerId, p.name.c_str(), p.endpoint.toString().c_str()); },
        [&](const std::string& r) { std::printf("disconnected: %s\n", r.c_str()); },
        [&](MsgType t, ByteReader& r) {
            if (t == MsgType::EntitySpawn) { EntitySpawn e; if (decode(r, e)) { reg.add(e); std::printf("spawn netId=%08X owner=%d frame=%s\n", e.netId, e.ownerPlayerId, frameKindName(e.frame.kind)); } }
            else if (t == MsgType::EntityDespawn) { EntityDespawn e; if (decode(r, e)) { reg.remove(e.netId); std::printf("despawn %08X\n", e.netId); } }
            else if (t == MsgType::Transform) { Transform e; if (decode(r, e)) { reg.applyTransform(e, nowMs()); } }
        });

    if (std::strcmp(argv[1], "host") == 0) {
        if (!s.host((uint16_t)std::atoi(argv[2]))) { std::printf("host failed: %s\n", s.lastError().c_str()); return 1; }
    } else if (std::strcmp(argv[1], "join") == 0 && argc >= 4) {
        Endpoint ep;
        if (!Endpoint::parse(argv[2], (uint16_t)std::atoi(argv[3]), ep)) { std::printf("bad ip\n"); return 1; }
        if (!s.join(ep)) { std::printf("join failed: %s\n", s.lastError().c_str()); return 1; }
    } else { std::printf("bad arguments\n"); return 2; }

    uint32_t lastSnap = 0, lastReport = 0;
    const uint32_t start = nowMs();
    for (;;) {
        uint32_t t = nowMs();
        s.update(t);
        if (s.state() == Session::State::Closed) break;
        if (s.isConnected()) {
            if (!spawnSent) {
                spawnSent = true;
                myEntity = reg.allocate(s.localPlayerId());
                EntitySpawn e; e.netId = myEntity; e.ownerPlayerId = s.localPlayerId(); e.kind = EntityKind::UFO; e.ufoType = 2;
                e.frame.kind = FrameKind::System; e.frame.starId = 0x123; e.rot = {0, 0, 0, 1};
                s.send(MsgType::EntitySpawn, toBytes(e));
            }
            if (t - lastSnap >= 1000u / cfg.snapshotHz) {
                lastSnap = t;
                float a = (t - start) / 1000.0f;
                Transform tr; tr.netId = myEntity; tr.tick = t; tr.frame.kind = FrameKind::System; tr.frame.starId = 0x123;
                tr.pos = {40.0f * std::cos(a), 40.0f * std::sin(a), 0.0f}; tr.rot = {0, 0, std::sin(a / 2), std::cos(a / 2)};
                tr.vel = {-40.0f * std::sin(a), 40.0f * std::cos(a), 0.0f};
                s.send(MsgType::Transform, toBytes(tr));
            }
            if (t - lastReport >= 1000) {
                lastReport = t;
                const Session::Stats& st = s.stats();
                std::printf("rtt=%ums sent=%u recv=%u resends=%u stale=%u entities=%zu", st.rttMs, st.packetsSent, st.packetsRecv, st.reliableResends, st.droppedStale, reg.size());
                for (auto& kv : reg.all()) {
                    if (kv.first == myEntity) continue;
                    Vec3 p; Quat q;
                    if (kv.second.interp.sample(t - 2000u / cfg.snapshotHz, p, q))
                        std::printf(" | ghost %08X at (%.1f %.1f %.1f) age=%ums", kv.first, p.x, p.y, p.z, t - kv.second.lastUpdateMs);
                }
                std::printf("\n");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0;
}
