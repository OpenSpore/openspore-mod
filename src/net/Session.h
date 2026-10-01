// Session.h - two-player UDP session: handshake, keepalive, reliable control channel,
// unreliable sequenced channel. Transport only; it knows nothing about Spore.
//
// Roles: the HOST opens a port and waits; the CLIENT sends Hello. The host is
// player 1, the client player 2. Only one peer is accepted (owner decision: 2 players).
#pragma once

#include "Protocol.h"
#include "UdpSocket.h"

#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace osmp {

class Session {
public:
    enum class Role : uint8_t { None, Host, Client };
    enum class State : uint8_t { Idle, Listening, Connecting, Connected, Closed };

    struct Config {
        uint8_t  snapshotHz = 15;
        uint32_t connectTimeoutMs = 5000;
        uint32_t keepaliveMs = 1000;
        uint32_t peerTimeoutMs = 8000;
        uint32_t resendMs = 200;
        uint32_t maxResends = 25;
        uint32_t gameBuild = 0;
        uint32_t politicalId = 0;
        std::string playerName = "player";
    };

    struct PeerInfo {
        uint8_t playerId = 0;
        uint32_t politicalId = 0;
        std::string name;
        Endpoint endpoint;
    };

    struct Stats {
        uint32_t packetsSent = 0, packetsRecv = 0;
        uint32_t bytesSent = 0, bytesRecv = 0;
        uint32_t reliableResends = 0;
        uint32_t droppedStale = 0;     // unreliable packets older than the last one seen
        uint32_t droppedDuplicate = 0; // reliable duplicates
        uint32_t rttMs = 0;
    };

    using ConnectedFn = std::function<void(const PeerInfo&)>;
    using DisconnectedFn = std::function<void(const std::string& reason)>;
    using MessageFn = std::function<void(MsgType type, ByteReader& payload)>;
    using LogFn = std::function<void(const std::string& line)>;

    Session();
    explicit Session(const Config& cfg);

    bool host(uint16_t port);
    bool join(const Endpoint& hostEndpoint, uint16_t localPort = 0);
    void leave(const char* reason = "leave");

    // Call every frame (or at least a few times per second) with a monotonic clock in ms.
    void update(uint32_t nowMs);

    // Reliability is chosen by message type (see isReliableType).
    bool send(MsgType type, const std::vector<uint8_t>& payload);
    bool sendReliable(MsgType type, const std::vector<uint8_t>& payload);
    bool sendUnreliable(MsgType type, const std::vector<uint8_t>& payload);

    State state() const { return state_; }
    Role role() const { return role_; }
    bool isConnected() const { return state_ == State::Connected; }
    uint8_t localPlayerId() const { return localPlayerId_; }
    uint32_t sessionId() const { return sessionId_; }
    const PeerInfo& peer() const { return peer_; }
    const Stats& stats() const { return stats_; }
    const Config& config() const { return cfg_; }
    uint16_t localPort() const { return socket_.localPort(); }
    size_t pendingReliable() const { return pending_.size(); }
    const std::string& lastError() const { return err_; }

    void setCallbacks(ConnectedFn onConnected, DisconnectedFn onDisconnected, MessageFn onMessage);
    void setLogger(LogFn log) { log_ = std::move(log); }

    // Test hook: drop this many inbound packets per 1000, deterministically.
    void setDebugDrop(uint32_t dropPermille, uint32_t seed = 1);

    static const char* stateName(State s);

private:
    struct Pending {
        MsgType type;
        std::vector<uint8_t> bytes;
        uint32_t lastSentMs;
        uint32_t attempts;
    };

    bool transmit(const Endpoint& to, MsgType type, uint8_t flags, uint16_t seq, const std::vector<uint8_t>& payload, uint32_t nowMs);
    void sendAck(const Endpoint& to, uint16_t id);
    void handlePacket(const Endpoint& from, const uint8_t* data, size_t len, uint32_t nowMs);
    bool noteReliableReceived(uint16_t id);
    bool isStaleUnreliable(MsgType type, uint16_t seq);
    void disconnect(const std::string& reason);
    void log(const std::string& line) { if (log_) log_(line); }

    Config cfg_;
    UdpSocket socket_;
    State state_ = State::Idle;
    Role role_ = Role::None;
    uint8_t localPlayerId_ = 0;
    uint32_t sessionId_ = 0;
    PeerInfo peer_;
    Stats stats_;
    std::string err_;

    uint32_t nowMs_ = 0;
    bool haveClock_ = false;
    uint32_t startedMs_ = 0;
    uint32_t lastRecvMs_ = 0;
    uint32_t lastPingMs_ = 0;
    uint16_t highestAckedByPeer_ = 0;

    uint16_t nextReliableId_ = 1;
    std::map<uint16_t, Pending> pending_;
    std::deque<uint16_t> recentReliable_;
    std::set<uint16_t> recentReliableSet_;

    uint16_t nextUnreliableSeq_[256];
    uint16_t lastUnreliableSeq_[256];
    bool haveUnreliableSeq_[256];

    uint32_t dropPermille_ = 0;
    uint32_t dropRng_ = 1;

    ConnectedFn onConnected_;
    DisconnectedFn onDisconnected_;
    MessageFn onMessage_;
    LogFn log_;
};

} // namespace osmp
