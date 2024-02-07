#define LWIP_COMPAT_SOCKETS 1

#define LWIP_SOCKET		 	1
#define NO_SYS                          1
#define MEM_ALIGNMENT                   4
#define LWIP_RAW                        1
#define LWIP_NETCONN                    0
#define LWIP_DHCP                       1
#define LWIP_ICMP                       1
#define LWIP_UDP                        0
#define LWIP_TCP                        1
#define ETH_PAD_SIZE                    0

#define TCP_MSS                         (1500 /*mtu*/ - 20 /*iphdr*/ - 20 /*tcphhr*/)
#define TCP_SND_BUF                     (2 * TCP_MSS)

#define LWIP_TIMEVAL_PRIVATE            0       /* Use the system-wide timeval definitions */
#define LWIP_NETIF_HOSTNAME		1
#define MEM_LIBC_MALLOC             0

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_DNS                    1
