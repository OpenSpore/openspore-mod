#include "test_framework.h"
#include "net/Protocol.h"
using namespace osmp;

TEST(header_roundtrip) {
    ByteWriter w; PacketHeader h; h.type = MsgType::Transform; h.flags = kFlagReliable; h.seq = 0xBEEF; h.ack = 0x1234;
    writeHeader(w, h);
    CHECK_EQ(w.size(), kHeaderSize);
    ByteReader r(w.data()); PacketHeader o;
    CHECK(readHeader(r, o));
    CHECK(o.type == MsgType::Transform); CHECK_EQ(o.flags, kFlagReliable); CHECK_EQ(o.seq, 0xBEEF); CHECK_EQ(o.ack, 0x1234);
}

TEST(header_rejects_bad_magic_and_version) {
    ByteWriter w; PacketHeader h; h.type = MsgType::Ping; writeHeader(w, h);
    std::vector<uint8_t> bad = w.data(); bad[0] ^= 0xFF;
    ByteReader r1(bad); PacketHeader o; CHECK(!readHeader(r1, o));
    std::vector<uint8_t> badVer = w.data(); badVer[4] = kProtocolVersion + 1;
    ByteReader r2(badVer); CHECK(!readHeader(r2, o));
    ByteReader r3(w.data().data(), 5); CHECK(!readHeader(r3, o));
}

TEST(hello_welcome_roundtrip) {
    Hello a; a.gameBuild = 3010029; a.politicalId = 0x800021CA; a.playerName = "nsvk13";
    std::vector<uint8_t> ab = toBytes(a); ByteReader r(ab); Hello b; CHECK(decode(r, b));
    CHECK_EQ(b.gameBuild, a.gameBuild); CHECK_EQ(b.politicalId, a.politicalId); CHECK(b.playerName == "nsvk13");
    Welcome wa; wa.playerId = 2; wa.sessionId = 42; wa.snapshotHz = 20; wa.hostPoliticalId = 7; wa.hostName = "host";
    std::vector<uint8_t> wab = toBytes(wa); ByteReader r2(wab); Welcome wb; CHECK(decode(r2, wb));
    CHECK_EQ(wb.playerId, 2); CHECK_EQ(wb.sessionId, 42u); CHECK_EQ(wb.snapshotHz, 20); CHECK(wb.hostName == "host");
}

TEST(transform_roundtrip_with_frame) {
    Transform t; t.netId = (2u << 24) | 5; t.tick = 123456;
    t.frame.kind = FrameKind::Planet; t.frame.starId = 0x00ABC123; t.frame.planetId = 0x02ABC123;
    t.pos = {284.72f, 514.96f, 1203.5f}; t.rot = {0.046f, -0.651f, 0.405f, 0.640f}; t.vel = {1.5f, -2.5f, 0.0f};
    std::vector<uint8_t> bytes = toBytes(t);
    CHECK_EQ(bytes.size(), 4u + 4u + 9u + 12u + 16u + 12u);
    ByteReader r(bytes); Transform o; CHECK(decode(r, o));
    CHECK_EQ(o.netId, t.netId); CHECK_EQ(o.tick, t.tick); CHECK(o.frame == t.frame);
    CHECK_NEAR(o.pos.x, 284.72f, 1e-6); CHECK_NEAR(o.rot.w, 0.640f, 1e-6); CHECK_NEAR(o.vel.y, -2.5f, 1e-6);
    CHECK_EQ(r.remaining(), 0u);
}

TEST(entity_spawn_roundtrip) {
    EntitySpawn s; s.netId = 0x01000001; s.ownerPlayerId = 1; s.kind = EntityKind::UFO; s.ufoType = 2; s.politicalId = 0x8000157B;
    s.model = {0x11, 0x22, 0x33}; s.frame.kind = FrameKind::System; s.frame.starId = 99; s.pos = {1, 2, 3}; s.rot = {0, 0, 0, 1};
    std::vector<uint8_t> sb = toBytes(s); ByteReader r(sb); EntitySpawn o; CHECK(decode(r, o));
    CHECK_EQ(o.netId, s.netId); CHECK_EQ(o.ownerPlayerId, 1); CHECK(o.kind == EntityKind::UFO); CHECK_EQ(o.ufoType, 2);
    CHECK_EQ(o.model.groupId, 0x33u); CHECK(o.frame == s.frame); CHECK_NEAR(o.pos.z, 3.0f, 1e-6);
}

TEST(truncated_payload_fails) {
    Transform t; std::vector<uint8_t> bytes = toBytes(t);
    for (size_t cut = 0; cut < bytes.size(); cut += 7) {
        ByteReader r(bytes.data(), cut); Transform o; CHECK(!decode(r, o));
    }
    ByteReader r(bytes.data(), bytes.size()); Transform o; CHECK(decode(r, o));
}

TEST(frame_decode_rejects_unknown_kind) {
    ByteWriter w; w.u8(9); w.u32(1); w.u32(2);
    ByteReader r(w.data()); Frame f; CHECK(!decode(r, f));
}

TEST(reliability_by_type) {
    CHECK(isReliableType(MsgType::Hello)); CHECK(isReliableType(MsgType::EntitySpawn)); CHECK(isReliableType(MsgType::FrameChange));
    CHECK(!isReliableType(MsgType::Transform)); CHECK(!isReliableType(MsgType::Ping)); CHECK(!isReliableType(MsgType::Ack));
}

TEST(strings_are_length_limited) {
    Hello a; a.playerName = std::string(300, 'x');
    std::vector<uint8_t> ab = toBytes(a); ByteReader r(ab); Hello b; CHECK(decode(r, b)); CHECK_EQ(b.playerName.size(), 255u);
}
