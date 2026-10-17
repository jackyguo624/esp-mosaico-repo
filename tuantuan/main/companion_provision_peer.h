// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

// ESP-IDF's HTTP server uses a dual-stack socket when IPv6 is enabled.
// IPv4 AP clients are therefore returned as ::ffff:192.168.4.x by getpeername.
static inline bool companion_provision_peer_allowed(const struct sockaddr *peer, socklen_t length)
{
    if (!peer || length < sizeof(struct sockaddr)) return false;
    uint32_t ipv4;
    if (peer->sa_family == AF_INET && length >= sizeof(struct sockaddr_in)) {
        ipv4 = ((const struct sockaddr_in *)peer)->sin_addr.s_addr;
    }
#if !defined(ESP_PLATFORM) || LWIP_IPV6
    else if (peer->sa_family == AF_INET6 && length >= sizeof(struct sockaddr_in6)) {
        const unsigned char *bytes = ((const struct sockaddr_in6 *)peer)->sin6_addr.s6_addr;
        static const unsigned char mapped_prefix[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
        if (memcmp(bytes, mapped_prefix, sizeof(mapped_prefix))) return false;
        memcpy(&ipv4, bytes + sizeof(mapped_prefix), sizeof(ipv4));
    }
#endif
    else return false;
    return (ntohl(ipv4) & 0xffffff00U) == 0xc0a80400U;
}
