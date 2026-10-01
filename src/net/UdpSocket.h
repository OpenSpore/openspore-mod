// UdpSocket.h - minimal non-blocking IPv4 UDP socket (WinSock on Windows, BSD sockets elsewhere).
#pragma once

#include <cstdint>
#include <string>

namespace osmp {

struct Endpoint {
    uint32_t ip = 0;    // IPv4 in host byte order
    uint16_t port = 0;

    bool valid() const { return port != 0; }
    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Endpoint& o) const { return !(*this == o); }

    std::string toString() const;
    // Accepts dotted-quad text only (no DNS), e.g. "192.168.1.10".
    static bool parse(const std::string& ipText, uint16_t port, Endpoint& out);
    static Endpoint loopback(uint16_t port) { Endpoint e; e.ip = 0x7F000001u; e.port = port; return e; }
};

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open(uint16_t port);                // 0 = let the OS pick a port
    void close();
    bool isOpen() const { return fd_ != -1; }

    bool send(const Endpoint& to, const void* data, size_t len);
    // returns number of bytes read, 0 when nothing is pending, -1 on error
    int recv(Endpoint& from, void* buf, size_t cap);

    uint16_t localPort() const { return port_; }
    const std::string& lastError() const { return err_; }

private:
    intptr_t fd_;
    uint16_t port_;
    std::string err_;
};

} // namespace osmp
