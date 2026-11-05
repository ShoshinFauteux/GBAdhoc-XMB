/* wincompat.h -- just enough POSIX on Winsock to build GBAdhoc's relay test
 * client (shared/relay/tests/relay_client.c) UNCHANGED with mingw, so the
 * online tests drive the desktop relay with the same simulated console the
 * online branch uses.  Test-only; never in the app. */
#ifndef XMB_WINCOMPAT_H
#define XMB_WINCOMPAT_H
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <time.h>
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0          /* the socket is made non-blocking instead */
#endif
#define F_SETFL 4
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x4000
#endif
static inline int wc_fcntl(int fd, int cmd, int fl)
{
   u_long nb = 1;
   (void)cmd; (void)fl;
   return ioctlsocket((SOCKET)fd, FIONBIO, &nb) == 0 ? 0 : -1;
}
#define fcntl(fd, cmd, fl) wc_fcntl(fd, cmd, fl)
static inline int wc_usleep(unsigned us)
{
   Sleep(us >= 1000 ? us / 1000 : 1);
   return 0;
}
#define usleep(us) wc_usleep(us)
#define close(fd) closesocket((SOCKET)(fd))
static void __attribute__((constructor)) wc_init(void)
{
   WSADATA w;
   WSAStartup(MAKEWORD(2, 2), &w);
   timeBeginPeriod(1);        /* 1 ms sleeps, as the 16 ms frame loop expects */
}
#endif
