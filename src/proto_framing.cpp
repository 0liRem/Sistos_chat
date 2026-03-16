#include "proto_framing.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

static bool recv_all(int fd, void* buf, size_t n) {
    size_t received = 0;
    char* ptr = reinterpret_cast<char*>(buf);
    while (received < n) {
        ssize_t r = recv(fd, ptr + received, n - received, 0);
        if (r <= 0) return false;
        received += r;
    }
    return true;
}


static bool send_all(int fd, const void* buf, size_t n) {
    size_t sent = 0;
    const char* ptr = reinterpret_cast<const char*>(buf);
    while (sent < n) {
        ssize_t s = send(fd, ptr + sent, n - sent, MSG_NOSIGNAL);
        if (s <= 0) return false;
        sent += s;
    }
    return true;
}
//5 bytes 
bool send_message(int fd, uint8_t type, const std::string& payload) {

    uint8_t header[5];
    header[0] = type;
    uint32_t length = htonl(static_cast<uint32_t>(payload.size()));
    memcpy(header + 1, &length, 4);

    if (!send_all(fd, header, 5)) return false;
    if (!payload.empty() && !send_all(fd, payload.data(), payload.size())) return false;
    return true;
}

bool recv_message(int fd, uint8_t& type, std::string& payload) {
    uint8_t header[5];
    if (!recv_all(fd, header, 5)) return false;

    type = header[0];
    uint32_t net_len;
    memcpy(&net_len, header + 1, 4);
    uint32_t length = ntohl(net_len);

    payload.resize(length);
    if (length > 0 && !recv_all(fd, payload.data(), length)) return false;
    return true;
}
