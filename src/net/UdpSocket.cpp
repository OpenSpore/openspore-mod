#include "UdpSocket.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "ws2_32.lib")
#  endif
typedef int socklen_t_compat;
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
typedef socklen_t socklen_t_compat;
#endif

namespace osmp {

namespace {

#ifdef _WIN32
struct WinsockInit {
    WinsockInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WinsockInit() { WSACleanup(); }
};
void ensureWinsock() { static WinsockInit init; (void)init; }
int lastSocketError() { return WSAGetLastError(); }
bool wouldBlock(int e) { return e == WSAEWOULDBLOCK; }
#else
void ensureWinsock() {}
int lastSocketError() { return errno; }
bool wouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN; }
#endif

void fillSockaddr(const Endpoint& ep, sockaddr_in& sa) {
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(ep.port);
    sa.sin_addr.s_addr = htonl(ep.ip);
}

} // namespace

std::string Endpoint::toString() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u",
                  (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, (unsigned)port);
    return buf;
}

bool Endpoint::parse(const std::string& ipText, uint16_t port, Endpoint& out) {
    unsigned a, b, c, d;
    char extra;
    if (std::sscanf(ipText.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    out.ip = (a << 24) | (b << 16) | (c << 8) | d;
    out.port = port;
    return true;
}

UdpSocket::UdpSocket() : fd_(-1), port_(0) { ensureWinsock(); }
UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(uint16_t port) {
    close();
    intptr_t fd = (intptr_t)::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0 || fd == (intptr_t)(~(uintptr_t)0)) { err_ = "socket() failed"; return false; }

    sockaddr_in sa;
    Endpoint any; any.ip = 0; any.port = port;
    fillSockaddr(any, sa);
    if (::bind((int)fd, (sockaddr*)&sa, sizeof(sa)) != 0) {
        err_ = "bind() failed, error " + std::to_string(lastSocketError());
#ifdef _WIN32
        ::closesocket((SOCKET)fd);
#else
        ::close((int)fd);
#endif
        return false;
    }

#ifdef _WIN32
    u_long nonBlocking = 1;
    ::ioctlsocket((SOCKET)fd, FIONBIO, &nonBlocking);
#else
    int flags = ::fcntl((int)fd, F_GETFL, 0);
    ::fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
#endif

    sockaddr_in bound;
    socklen_t_compat blen = sizeof(bound);
    if (::getsockname((int)fd, (sockaddr*)&bound, &blen) == 0) port_ = ntohs(bound.sin_port);
    else port_ = port;

    fd_ = fd;
    err_.clear();
    return true;
}

void UdpSocket::close() {
    if (fd_ == -1) return;
#ifdef _WIN32
    ::closesocket((SOCKET)fd_);
#else
    ::close((int)fd_);
#endif
    fd_ = -1;
    port_ = 0;
}

bool UdpSocket::send(const Endpoint& to, const void* data, size_t len) {
    if (fd_ == -1) return false;
    sockaddr_in sa;
    fillSockaddr(to, sa);
    int n = ::sendto((int)fd_, (const char*)data, (int)len, 0, (sockaddr*)&sa, sizeof(sa));
    if (n < 0) { err_ = "sendto() failed, error " + std::to_string(lastSocketError()); return false; }
    return (size_t)n == len;
}

int UdpSocket::recv(Endpoint& from, void* buf, size_t cap) {
    if (fd_ == -1) return -1;
    sockaddr_in sa;
    socklen_t_compat slen = sizeof(sa);
    int n = ::recvfrom((int)fd_, (char*)buf, (int)cap, 0, (sockaddr*)&sa, &slen);
    if (n < 0) {
        int e = lastSocketError();
        if (wouldBlock(e)) return 0;
#ifdef _WIN32
        // ICMP port unreachable surfaces as WSAECONNRESET on UDP sockets; not fatal.
        if (e == WSAECONNRESET) return 0;
#endif
        err_ = "recvfrom() failed, error " + std::to_string(e);
        return -1;
    }
    from.ip = ntohl(sa.sin_addr.s_addr);
    from.port = ntohs(sa.sin_port);
    return n;
}

} // namespace osmp
