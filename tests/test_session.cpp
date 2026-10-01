#include <set>
#include "test_framework.h"
#include "net/Session.h"
using namespace osmp;

namespace {
struct Pair {
    Session host, client;
    uint32_t now = 1000;
    std::vector<std::pair<MsgType, std::vector<uint8_t>>> hostGot, clientGot;
    std::string hostDisc, clientDisc;
    int hostConnected = 0, clientConnected = 0;

    Pair(Session::Config hc = Session::Config(), Session::Config cc = Session::Config()) : host(hc), client(cc) {
        host.setCallbacks([&](const Session::PeerInfo&) { hostConnected++; }, [&](const std::string& r) { hostDisc = r; },
                          [&](MsgType t, ByteReader& r) { std::vector<uint8_t> b(r.remaining()); r.bytes(b.data(), b.size()); hostGot.push_back({t, b}); });
        client.setCallbacks([&](const Session::PeerInfo&) { clientConnected++; }, [&](const std::string& r) { clientDisc = r; },
                            [&](MsgType t, ByteReader& r) { std::vector<uint8_t> b(r.remaining()); r.bytes(b.data(), b.size()); clientGot.push_back({t, b}); });
    }
    // advance virtual time, pumping both ends; real UDP on loopback
    void pump(int steps, uint32_t stepMs = 10) {
        for (int i = 0; i < steps; ++i) { now += stepMs; host.update(now); client.update(now); }
    }
    bool connect() {
        if (!host.host(0)) return false;
        host.update(now);
        if (!client.join(Endpoint::loopback(host.localPort()))) return false;
        for (int i = 0; i < 200 && !(host.isConnected() && client.isConnected()); ++i) pump(1);
        return host.isConnected() && client.isConnected();
    }
};
}

TEST(session_handshake_over_loopback) {
    Pair p;
    CHECK(p.connect());
    CHECK_EQ(p.host.localPlayerId(), 1); CHECK_EQ(p.client.localPlayerId(), 2);
    CHECK_EQ(p.host.peer().playerId, 2); CHECK_EQ(p.client.peer().playerId, 1);
    CHECK_EQ(p.hostConnected, 1); CHECK_EQ(p.clientConnected, 1);
    CHECK_EQ(p.client.sessionId(), p.host.sessionId());
    p.pump(50);
    CHECK_EQ(p.host.pendingReliable(), 0u); CHECK_EQ(p.client.pendingReliable(), 0u);   // Hello/Welcome acked
}

TEST(session_reliable_and_unreliable_delivery) {
    Pair p; CHECK(p.connect());
    EntitySpawn s; s.netId = 0x02000001; s.ownerPlayerId = 2;
    CHECK(p.client.send(MsgType::EntitySpawn, toBytes(s)));
    Transform t; t.netId = s.netId; t.tick = 1;
    CHECK(p.client.send(MsgType::Transform, toBytes(t)));
    p.pump(5);
    CHECK_EQ(p.hostGot.size(), 2u);
    if (p.hostGot.size() == 2) {
        CHECK(p.hostGot[0].first == MsgType::EntitySpawn); CHECK(p.hostGot[1].first == MsgType::Transform);
        ByteReader r(p.hostGot[0].second); EntitySpawn o; CHECK(decode(r, o)); CHECK_EQ(o.netId, s.netId);
    }
}

TEST(session_stale_unreliable_dropped_and_rtt_measured) {
    Pair p; CHECK(p.connect());
    for (uint32_t i = 1; i <= 5; ++i) { Transform t; t.tick = i; p.host.send(MsgType::Transform, toBytes(t)); }
    p.pump(3);
    CHECK_EQ(p.clientGot.size(), 5u);
    p.pump(150);   // > keepalive: pings flow, pongs come back
    CHECK(p.host.stats().rttMs <= 20);
    CHECK(p.client.stats().packetsRecv > 5);
}

TEST(session_reliable_survives_packet_loss) {
    Session::Config hc; hc.resendMs = 20;
    Session::Config cc; cc.resendMs = 20;
    Pair p(hc, cc); CHECK(p.connect());
    p.host.setDebugDrop(400, 7);   // drop 40% of what the host receives
    for (uint32_t i = 0; i < 20; ++i) { EntityDespawn d; d.netId = i; p.client.send(MsgType::EntityDespawn, toBytes(d)); }
    p.pump(100);
    CHECK_EQ(p.hostGot.size(), 20u);
    CHECK(p.client.stats().reliableResends > 0);
    CHECK_EQ(p.client.pendingReliable(), 0u);
    // duplicates created by resends must not be delivered twice
    std::set<uint32_t> ids; for (auto& m : p.hostGot) { ByteReader r(m.second); EntityDespawn d; decode(r, d); ids.insert(d.netId); }
    CHECK_EQ(ids.size(), 20u);
}

TEST(session_second_client_is_rejected) {
    Pair p; CHECK(p.connect());
    Session third; std::string reason;
    third.setCallbacks(nullptr, [&](const std::string& r) { reason = r; }, nullptr);
    CHECK(third.join(Endpoint::loopback(p.host.localPort())));
    for (int i = 0; i < 20 && reason.empty(); ++i) { p.pump(1); third.update(p.now); }
    CHECK(third.state() == Session::State::Closed);
    CHECK(reason.find("rejected") != std::string::npos);
}

TEST(session_bye_and_timeout) {
    Pair p; CHECK(p.connect());
    p.client.leave();
    p.pump(5);
    CHECK(!p.host.isConnected()); CHECK(p.hostDisc == "peer left");

    Session::Config cc; cc.peerTimeoutMs = 300;
    Pair q(Session::Config(), cc); CHECK(q.connect());
    q.host.leave();                           // host vanishes silently from the client's point of view? no: Bye is sent
    q.pump(5);
    CHECK(!q.client.isConnected());

    Session::Config cc2; cc2.connectTimeoutMs = 200;
    Session lonely(cc2); std::string why;
    lonely.setCallbacks(nullptr, [&](const std::string& r) { why = r; }, nullptr);
    CHECK(lonely.join(Endpoint::loopback(1)));   // nobody listens on port 1
    for (uint32_t t = 0; t < 400; t += 10) lonely.update(t);
    CHECK(why == "connect timeout");
}

TEST(session_version_mismatch_rejected) {
    Pair p; CHECK(p.host.host(0)); p.host.update(p.now);
    // hand-craft a Hello with a wrong protocol version
    UdpSocket s; CHECK(s.open(0));
    ByteWriter w; PacketHeader h; h.version = kProtocolVersion + 1; h.type = MsgType::Hello; h.flags = kFlagReliable; h.seq = 1; writeHeader(w, h);
    Hello hello; encode(w, hello);
    CHECK(s.send(Endpoint::loopback(p.host.localPort()), w.data().data(), w.size()));
    p.pump(3);
    uint8_t buf[256]; Endpoint from; int n = 0;
    for (int i = 0; i < 10 && n <= 0; ++i) n = s.recv(from, buf, sizeof(buf));
    CHECK(n > 0);
    if (n > 0) { ByteReader r(buf, n); PacketHeader rh; CHECK(readHeader(r, rh)); CHECK(rh.type == MsgType::Reject); Reject rj; CHECK(decode(r, rj)); CHECK(rj.reason == RejectReason::VersionMismatch); }
    CHECK(!p.host.isConnected());
}
