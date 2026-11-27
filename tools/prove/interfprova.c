/* =============================================================================
 * tools/prove/interfprova.c - the named interfaces (0.373): getifaddrs,
 * if_nameindex and family, SIOCGIF* on a socket
 *
 * Lines tools/prova_interfacce.sh reads:
 *   IFA <name> <family> ind=<a.b.c.d|mac> masc=<..> bc=<..> flag=<hex>
 *   NI <index> <name>                       if_nameindex
 *   N2I eth0=<n> lo=<n> xx=<n>              if_nametoindex
 *   I2N 1=<name> 2=<name> 9=<name|NULL>     if_indextoname
 *   CONF n=<count> <name>=<addr> ...        SIOCGIFCONF
 *   GIF eth0 flag=<hex> ind=.. masc=.. bc=.. mtu=.. idx=.. hw=<mac>
 *   GIF xx <errno>                          a name that is not there
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const char *ind(const struct sockaddr *a, char *b)
{
    if (!a) return "-";
    if (a->sa_family == AF_INET) {
        const unsigned char *p = (const unsigned char *)&((const struct sockaddr_in *)a)->sin_addr;
        sprintf(b, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    } else if (a->sa_family == AF_PACKET) {
        const unsigned char *m = ((const struct sockaddr_ll *)a)->sll_addr;
        sprintf(b, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    } else sprintf(b, "fam%d", a->sa_family);
    return b;
}

int main(void)
{
    struct ifaddrs      *l, *a;
    struct if_nameindex *ni, *e;
    struct ifreq         q, v[8];
    struct ifconf        c;
    char                 b1[40], b2[40], b3[40], nome[IF_NAMESIZE];
    int                  s, i, n;

    if (getifaddrs(&l) != 0) { printf("IFA ERRORE %d\n", errno); return 1; }
    for (a = l; a; a = a->ifa_next)
        printf("IFA %s %s ind=%s masc=%s bc=%s flag=%x\n", a->ifa_name,
               a->ifa_addr ? (a->ifa_addr->sa_family == AF_INET ? "inet" : "packet") : "-",
               ind(a->ifa_addr, b1), ind(a->ifa_netmask, b2), ind(a->ifa_broadaddr, b3), a->ifa_flags);
    freeifaddrs(l);

    ni = if_nameindex();
    for (e = ni; e && e->if_index; e++) printf("NI %u %s\n", e->if_index, e->if_name);
    if_freenameindex(ni);
    printf("N2I eth0=%u lo=%u xx=%u\n", if_nametoindex("eth0"), if_nametoindex("lo"), if_nametoindex("xx"));
    printf("I2N 1=%s", if_indextoname(1, nome) ? nome : "NULL");
    printf(" 2=%s", if_indextoname(2, nome) ? nome : "NULL");
    printf(" 9=%s\n", if_indextoname(9, nome) ? nome : "NULL");

    s = socket(AF_INET, SOCK_DGRAM, 0);
    c.ifc_len = sizeof(v); c.ifc_req = v;
    if (ioctl(s, SIOCGIFCONF, &c) != 0) printf("CONF ERRORE %d\n", errno);
    else {
        n = c.ifc_len / (int)sizeof(struct ifreq);
        printf("CONF n=%d", n);
        for (i = 0; i < n; i++) printf(" %s=%s", v[i].ifr_name, ind(&v[i].ifr_addr, b1));
        printf("\n");
    }
    memset(&q, 0, sizeof(q)); strcpy(q.ifr_name, "eth0");
    printf("GIF eth0");
    if (ioctl(s, SIOCGIFFLAGS, &q) == 0) printf(" flag=%x", (unsigned short)q.ifr_flags);
    if (ioctl(s, SIOCGIFADDR, &q) == 0) printf(" ind=%s", ind(&q.ifr_addr, b1));
    if (ioctl(s, SIOCGIFNETMASK, &q) == 0) printf(" masc=%s", ind(&q.ifr_netmask, b1));
    if (ioctl(s, SIOCGIFBRDADDR, &q) == 0) printf(" bc=%s", ind(&q.ifr_broadaddr, b1));
    if (ioctl(s, SIOCGIFMTU, &q) == 0) printf(" mtu=%d", q.ifr_mtu);
    if (ioctl(s, SIOCGIFINDEX, &q) == 0) printf(" idx=%d", q.ifr_ifindex);
    if (ioctl(s, SIOCGIFHWADDR, &q) == 0) {
        const unsigned char *m = (const unsigned char *)q.ifr_hwaddr.sa_data;
        printf(" hw=%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    }
    printf("\n");
    memset(&q, 0, sizeof(q)); strcpy(q.ifr_name, "xx");
    printf("GIF xx %d\n", ioctl(s, SIOCGIFFLAGS, &q) == 0 ? 0 : errno);
    close(s);
    printf("INTERF FINE\n");
    return 0;
}
