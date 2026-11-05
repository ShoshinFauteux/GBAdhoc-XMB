/* upnp.h -- a small UPnP Internet Gateway Device client (Winsock): SSDP
 * discovery, the device description, and the WANIPConnection /
 * WANPPPConnection actions the Online tab needs (GetExternalIPAddress,
 * AddPortMapping, DeletePortMapping, GetSpecificPortMappingEntry).
 *
 * Plain HTTP over a raw TCP socket (an IGD speaks nothing else), every step
 * bounded by a timeout.  A description or control URL is only followed when
 * its host is a private or loopback IPv4 literal: an SSDP answer cannot send
 * the app to the internet.  Never touches the Windows firewall.
 */
#ifndef XMB_UPNP_H
#define XMB_UPNP_H

#include <stddef.h>

typedef struct
{
   char ssdp[64];          /* "ip:port" to send M-SEARCH to; "" = 239.255.255.250:1900 */
   unsigned wait_ms;       /* how long to wait for SSDP answers; 0 = 2500 */
   unsigned http_ms;       /* per HTTP exchange; 0 = 3000 */
} upnp_opts;

typedef struct
{
   char host[16];          /* the IGD's IPv4 */
   unsigned short port;
   char control[512];      /* path of the control URL */
   char service[96];       /* urn:schemas-upnp-org:service:WANIPConnection:1 ... */
   char friendly[96];      /* the router's friendlyName */
   char local_ip[16];      /* this PC's address on the IGD's network */
   unsigned http_ms;
} upnp_igd;

enum { UPNP_OK = 0, UPNP_NONE = 1, UPNP_HTTP = 2, UPNP_SOAP = 3 };

/* 0 = found an IGD with a WAN connection service. */
int upnp_discover(const upnp_opts *o, upnp_igd *igd, char *err, size_t cap);
int upnp_external_ip(const upnp_igd *igd, char ip[16], char *err, size_t cap);
/* *soap_code: the UPnP errorCode of a refusal (718 conflict, 725 only
 * permanent leases, ...), 0 otherwise. */
int upnp_add(const upnp_igd *igd, unsigned short ext, unsigned short in, const char *proto,
             unsigned lease_s, const char *desc, int *soap_code, char *err, size_t cap);
int upnp_delete(const upnp_igd *igd, unsigned short ext, const char *proto, int *soap_code,
                char *err, size_t cap);
/* who holds ext/proto: client[16] and internal port; 0 = a mapping exists */
int upnp_get_mapping(const upnp_igd *igd, unsigned short ext, const char *proto,
                     char client[16], unsigned short *in_port, char *err, size_t cap);

/* What an IPv4 address is, for the reachability verdict. */
enum { IPC_PUBLIC = 0, IPC_PRIVATE, IPC_CGNAT, IPC_LOOPBACK, IPC_LINKLOCAL, IPC_INVALID };
int upnp_ip_class(const char *ip);
const char *upnp_ip_class_name(int c);

#endif
