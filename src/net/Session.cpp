#include "Session.h"

#include <cstring>

namespace osmp {

namespace {
int16_t seqDiff(uint16_t a, uint16_t b) { return (int16_t)(a - b); }
uint32_t mix(uint32_t x) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x ? x : 1; }
} // namespace

Session::Session() : Session(Config()) {}

Session::Session(const Config& cfg) : cfg_(cfg) {
    std::memset(nextUnreliableSeq_, 0, sizeof(nextUnreliableSeq_));
    std::memset(lastUnreliableSeq_, 0, sizeof(lastUnreliableSeq_));
    std::memset(haveUnreliableSeq_, 0, sizeof(haveUnreliableSeq_));
}

const char* Session::stateName(State s) {
    switch (s) {
    case State::Idle: return "idle";
    case State::Listening: return "listening";
    case State::Connecting: return "connecting";
    case State::Connected: return "connected";
    case State::Closed: return "closed";
    }
    return "?";
}

void Session::setCallbacks(ConnectedFn onConnected, DisconnectedFn onDisconnected, MessageFn onMessage) {
    onConnected_ = std::move(onConnected);
    onDisconnected_ = std::move(onDisconnected);
    onMessage_ = std::move(onMessage);
}

void Session::setDebugDrop(uint32_t dropPermille, uint32_t seed) {
    dropPermille_ = dropPermille > 1000 ? 1000 : dropPermille;
    dropRng_ = seed ? seed : 1;
}

bool Session::host(uint16_t port) {
    leave("rehost");
    if (!socket_.open(port)) { err_ = socket_.lastError(); return false; }
    role_ = Role::Host;
    state_ = State::Listening;
    localPlayerId_ = 1;
    sessionId_ = mix((uint32_t)(uintptr_t)this ^ 0x9E3779B9u ^ (uint32_t)port);
    haveClock_ = false;
    log("host: listening on udp port " + std::to_string(socket_.localPort()));
    return true;
}

bool Session::join(const Endpoint& hostEndpoint, uint16_t localPort) {
    leave("rejoin");
    if (!hostEndpoint.valid()) { err_ = "invalid host endpoint"; return false; }
    if (!socket_.open(localPort)) { err_ = socket_.lastError(); return false; }
    role_ = Role::Client;
    state_ = State::Connecting;
    localPlayerId_ = 0;
    peer_ = PeerInfo();
    peer_.endpoint = hostEndpoint;
    haveClock_ = false;

    Hello h;
    h.protocolVersion = kProtocolVersion;
    h.gameBuild = cfg_.gameBuild;
    h.politicalId = cfg_.politicalId;
    h.playerName = cfg_.playerName;
    log("join: connecting to " + hostEndpoint.toString());
    return sendReliable(MsgType::Hello, toBytes(h));
}

void Session::leave(const char* reason) {
    if (state_ == State::Idle || state_ == State::Closed) {
        if (state_ == State::Closed) state_ = State::Idle;
        return;
    }
    if (state_ == State::Connected && peer_.endpoint.valid()) {
        std::vector<uint8_t> empty;
        transmit(peer_.endpoint, MsgType::Bye, 0, 0, empty, nowMs_);
    }
    log(std::string("leave: ") + reason);
    socket_.close();
    pending_.clear();
    recentReliable_.clear();
    recentReliableSet_.clear();
    std::memset(haveUnreliableSeq_, 0, sizeof(haveUnreliableSeq_));
    state_ = State::Idle;
    role_ = Role::None;
    peer_ = PeerInfo();
}

void Session::disconnect(const std::string& reason) {
    if (state_ == State::Idle || state_ == State::Closed) return;
    log("disconnect: " + reason);
    socket_.close();
    pending_.clear();
    state_ = State::Closed;
    role_ = Role::None;
    if (onDisconnected_) onDisconnected_(reason);
}

bool Session::transmit(const Endpoint& to, MsgType type, uint8_t flags, uint16_t seq, const std::vector<uint8_t>& payload, uint32_t nowMs) {
    (void)nowMs;
    if (!socket_.isOpen()) return false;
    ByteWriter w(kHeaderSize + payload.size());
    PacketHeader h;
    h.type = type;
    h.flags = flags;
    h.seq = seq;
    h.ack = highestAckedByPeer_;
    writeHeader(w, h);
    w.bytes(payload.data(), payload.size());
    if (w.size() > kMaxPacketSize) { err_ = "packet too large"; return false; }
    bool ok = socket_.send(to, w.data().data(), w.size());
    if (ok) { stats_.packetsSent++; stats_.bytesSent += (uint32_t)w.size(); }
    return ok;
}

void Session::sendAck(const Endpoint& to, uint16_t id) {
    std::vector<uint8_t> empty;
    transmit(to, MsgType::Ack, 0, id, empty, nowMs_);
}

bool Session::send(MsgType type, const std::vector<uint8_t>& payload) {
    return isReliableType(type) ? sendReliable(type, payload) : sendUnreliable(type, payload);
}

bool Session::sendReliable(MsgType type, const std::vector<uint8_t>& payload) {
    if (!(state_ == State::Connected || (state_ == State::Connecting && type == MsgType::Hello))) return false;
    uint16_t id = nextReliableId_++;
    if (nextReliableId_ == 0) nextReliableId_ = 1;
    Pending p;
    p.type = type;
    p.bytes = payload;
    p.lastSentMs = nowMs_;
    p.attempts = 1;
    pending_[id] = p;
    return transmit(peer_.endpoint, type, kFlagReliable, id, payload, nowMs_);
}

bool Session::sendUnreliable(MsgType type, const std::vector<uint8_t>& payload) {
    if (state_ != State::Connected) return false;
    uint16_t seq = nextUnreliableSeq_[(uint8_t)type]++;
    return transmit(peer_.endpoint, type, 0, seq, payload, nowMs_);
}

bool Session::noteReliableReceived(uint16_t id) {
    if (recentReliableSet_.count(id)) return false;
    recentReliable_.push_back(id);
    recentReliableSet_.insert(id);
    while (recentReliable_.size() > 1024) {
        recentReliableSet_.erase(recentReliable_.front());
        recentReliable_.pop_front();
    }
    return true;
}

bool Session::isStaleUnreliable(MsgType type, uint16_t seq) {
    uint8_t t = (uint8_t)type;
    if (haveUnreliableSeq_[t] && seqDiff(seq, lastUnreliableSeq_[t]) <= 0) return true;
    haveUnreliableSeq_[t] = true;
    lastUnreliableSeq_[t] = seq;
    return false;
}

void Session::handlePacket(const Endpoint& from, const uint8_t* data, size_t len, uint32_t nowMs) {
    ByteReader r(data, len);
    PacketHeader h;
    if (!readHeader(r, h)) {
        // A Hello with a wrong protocol version still deserves an answer, so peek at it.
        if (len >= kHeaderSize && (role_ == Role::Host)) {
            ByteReader peek(data, len);
            uint32_t magic = peek.u32(); uint8_t ver = peek.u8(); MsgType t = (MsgType)peek.u8();
            if (magic == kMagic && ver != kProtocolVersion && t == MsgType::Hello) {
                Reject rj; rj.reason = RejectReason::VersionMismatch;
                std::vector<uint8_t> b = toBytes(rj);
                transmit(from, MsgType::Reject, 0, 0, b, nowMs);
            }
        }
        return;
    }
    stats_.packetsRecv++;
    stats_.bytesRecv += (uint32_t)len;

    const bool fromPeer = peer_.endpoint.valid() && from == peer_.endpoint;

    // ---- host: accept exactly one client -------------------------------------
    if (role_ == Role::Host && h.type == MsgType::Hello) {
        Hello hello;
        if (!decode(r, hello)) return;
        if (state_ == State::Connected && !fromPeer) {
            Reject rj; rj.reason = RejectReason::SessionFull;
            std::vector<uint8_t> b = toBytes(rj);
            transmit(from, MsgType::Reject, 0, 0, b, nowMs);
            return;
        }
        if (h.flags & kFlagReliable) sendAck(from, h.seq);
        if (state_ == State::Connected && fromPeer) return;   // duplicate Hello after we answered
        if (hello.gameBuild != 0 && cfg_.gameBuild != 0 && hello.gameBuild != cfg_.gameBuild) {
            Reject rj; rj.reason = RejectReason::BuildMismatch;
            std::vector<uint8_t> b = toBytes(rj);
            transmit(from, MsgType::Reject, 0, 0, b, nowMs);
            return;
        }
        peer_.playerId = 2;
        peer_.politicalId = hello.politicalId;
        peer_.name = hello.playerName;
        peer_.endpoint = from;
        state_ = State::Connected;
        lastRecvMs_ = nowMs;
        lastPingMs_ = nowMs;

        Welcome w;
        w.playerId = 2;
        w.sessionId = sessionId_;
        w.snapshotHz = cfg_.snapshotHz;
        w.hostPoliticalId = cfg_.politicalId;
        w.hostName = cfg_.playerName;
        sendReliable(MsgType::Welcome, toBytes(w));
        log("host: client connected from " + from.toString() + " (" + hello.playerName + ")");
        if (onConnected_) onConnected_(peer_);
        return;
    }

    if (!fromPeer) return;   // everything else must come from our peer
    lastRecvMs_ = nowMs;
    if (seqDiff(h.ack, highestAckedByPeer_) > 0) highestAckedByPeer_ = h.ack;

    switch (h.type) {
    case MsgType::Ack: {
        pending_.erase(h.seq);
        return;
    }
    case MsgType::Reject: {
        Reject rj;
        if (decode(r, rj)) disconnect("rejected by host, reason " + std::to_string((int)rj.reason));
        return;
    }
    case MsgType::Bye: {
        if (h.flags & kFlagReliable) sendAck(from, h.seq);
        disconnect("peer left");
        return;
    }
    case MsgType::Ping: {
        Ping p;
        if (!decode(r, p)) return;
        Pong q; q.timeMs = p.timeMs;
        std::vector<uint8_t> b = toBytes(q);
        transmit(from, MsgType::Pong, 0, 0, b, nowMs);
        return;
    }
    case MsgType::Pong: {
        Pong q;
        if (!decode(r, q)) return;
        stats_.rttMs = nowMs >= q.timeMs ? nowMs - q.timeMs : 0;
        return;
    }
    default:
        break;
    }

    if (h.flags & kFlagReliable) {
        sendAck(from, h.seq);
        if (!noteReliableReceived(h.seq)) { stats_.droppedDuplicate++; return; }
    } else {
        if (isStaleUnreliable(h.type, h.seq)) { stats_.droppedStale++; return; }
    }

    if (h.type == MsgType::Welcome) {
        if (role_ != Role::Client) return;
        Welcome w;
        if (!decode(r, w)) return;
        if (state_ == State::Connected) return;
        localPlayerId_ = w.playerId;
        sessionId_ = w.sessionId;
        peer_.playerId = 1;
        peer_.politicalId = w.hostPoliticalId;
        peer_.name = w.hostName;
        state_ = State::Connected;
        lastPingMs_ = nowMs;
        log("client: connected, we are player " + std::to_string((int)localPlayerId_));
        if (onConnected_) onConnected_(peer_);
        return;
    }

    if (state_ != State::Connected) return;
    ByteReader payload(data + kHeaderSize, len - kHeaderSize);
    if (onMessage_) onMessage_(h.type, payload);
}

void Session::update(uint32_t nowMs) {
    nowMs_ = nowMs;
    if (!haveClock_) {
        haveClock_ = true;
        startedMs_ = nowMs;
        lastRecvMs_ = nowMs;
        lastPingMs_ = nowMs;
        for (auto& kv : pending_) kv.second.lastSentMs = nowMs;
    }
    if (state_ == State::Idle || state_ == State::Closed) return;

    // ---- receive -------------------------------------------------------------
    uint8_t buf[kMaxPacketSize + 64];
    for (int i = 0; i < 256; ++i) {
        Endpoint from;
        int n = socket_.recv(from, buf, sizeof(buf));
        if (n <= 0) break;
        if (dropPermille_) {
            dropRng_ = mix(dropRng_);
            if ((dropRng_ % 1000) < dropPermille_) continue;
        }
        handlePacket(from, buf, (size_t)n, nowMs);
        if (state_ == State::Idle || state_ == State::Closed) return;
    }

    // ---- reliable resends ----------------------------------------------------
    for (auto it = pending_.begin(); it != pending_.end();) {
        Pending& p = it->second;
        if (nowMs - p.lastSentMs >= cfg_.resendMs) {
            if (p.attempts >= cfg_.maxResends) {
                disconnect("peer unreachable (reliable message " + std::string(msgTypeName(p.type)) + " never acked)");
                return;
            }
            p.attempts++;
            p.lastSentMs = nowMs;
            stats_.reliableResends++;
            transmit(peer_.endpoint, p.type, kFlagReliable, it->first, p.bytes, nowMs);
        }
        ++it;
    }

    // ---- timeouts and keepalive ---------------------------------------------
    if (state_ == State::Connecting) {
        if (nowMs - startedMs_ > cfg_.connectTimeoutMs) disconnect("connect timeout");
        return;
    }
    if (state_ == State::Connected) {
        if (nowMs - lastRecvMs_ > cfg_.peerTimeoutMs) { disconnect("peer timeout"); return; }
        if (nowMs - lastPingMs_ >= cfg_.keepaliveMs) {
            lastPingMs_ = nowMs;
            Ping p; p.timeMs = nowMs;
            std::vector<uint8_t> b = toBytes(p);
            transmit(peer_.endpoint, MsgType::Ping, 0, 0, b, nowMs);
        }
    }
}

} // namespace osmp
