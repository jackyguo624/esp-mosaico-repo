// SPDX-License-Identifier: Apache-2.0
#include "companion_provision_peer.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static void check4(const char *ip, bool expected)
{
    struct sockaddr_in peer = {.sin_family = AF_INET};
    assert(inet_pton(AF_INET,ip,&peer.sin_addr)==1);
    assert(companion_provision_peer_allowed((struct sockaddr*)&peer,sizeof(peer))==expected);
    assert(!companion_provision_peer_allowed((struct sockaddr*)&peer,0));
}

static void check6(const char *ip, bool expected)
{
    struct sockaddr_in6 peer = {.sin6_family = AF_INET6};
    assert(inet_pton(AF_INET6,ip,&peer.sin6_addr)==1);
    assert(companion_provision_peer_allowed((struct sockaddr*)&peer,sizeof(peer))==expected);
    assert(!companion_provision_peer_allowed((struct sockaddr*)&peer,sizeof(struct sockaddr_in)));
}

int main(void)
{
    check4("192.168.4.2",true);
    check4("192.168.4.254",true);
    check4("192.168.5.2",false);
    check4("192.0.2.10",false);
    check4("127.0.0.1",false);
    check6("::ffff:192.168.4.2",true);
    check6("::ffff:192.168.4.254",true);
    check6("::ffff:192.168.5.2",false);
    check6("::ffff:127.0.0.1",false);
    check6("::192.168.4.2",false);
    check6("::1",false);
    check6("fe80::1",false);
    assert(!companion_provision_peer_allowed(NULL,0));
    struct sockaddr unknown = {.sa_family=AF_UNSPEC};
    assert(!companion_provision_peer_allowed(&unknown,sizeof(unknown)));

    // Exercise the actual dual-stack getpeername path, not just constructed bytes.
    int listener = socket(AF_INET6,SOCK_STREAM,0);
    assert(listener>=0);
    int off=0;
    assert(!setsockopt(listener,IPPROTO_IPV6,IPV6_V6ONLY,&off,sizeof(off)));
    struct sockaddr_in6 listen_address = {.sin6_family=AF_INET6,.sin6_addr=IN6ADDR_ANY_INIT};
    assert(!bind(listener,(struct sockaddr*)&listen_address,sizeof(listen_address)));
    assert(!listen(listener,1));
    socklen_t length=sizeof(listen_address);
    assert(!getsockname(listener,(struct sockaddr*)&listen_address,&length));
    int client=socket(AF_INET,SOCK_STREAM,0);
    assert(client>=0);
    struct sockaddr_in target = {.sin_family=AF_INET,.sin_port=listen_address.sin6_port};
    assert(inet_pton(AF_INET,"127.0.0.1",&target.sin_addr)==1);
    assert(!connect(client,(struct sockaddr*)&target,sizeof(target)));
    int accepted=accept(listener,NULL,NULL);
    assert(accepted>=0);
    struct sockaddr_storage actual = {0};
    length=sizeof(actual);
    assert(!getpeername(accepted,(struct sockaddr*)&actual,&length));
    assert(actual.ss_family==AF_INET6);
    assert(!companion_provision_peer_allowed((struct sockaddr*)&actual,length));
    // Keep the returned family/length and substitute the AP client's IPv4 suffix.
    unsigned char *bytes=((struct sockaddr_in6*)&actual)->sin6_addr.s6_addr;
    const unsigned char ap_ip[4]={192,168,4,2};
    memcpy(bytes+12,ap_ip,sizeof(ap_ip));
    assert(companion_provision_peer_allowed((struct sockaddr*)&actual,length));
    puts("PASS: IPv4, IPv4-mapped IPv6, AP isolation, truncated addresses and real dual-stack socket.");
    return 0;
}
