/* main.c -- GBAdhoc XMB: a standalone PSP app that gives each GBA/GB/GBC
 * game its own entry in the XMB's Game column (docs/XMB-LAUNCH.md).
 *
 * One loop at 60 fps: read the pad, draw the screen (ui.c), swap.  All
 * Memory Stick, decoding, network and entry writing runs on the worker
 * (jobs.c).
 *
 * HARNESS.  <app dir>/xmbapp-harness.ini drives the app without hands, for
 * PPSSPP runs (tools/run_ppsspp.sh):
 *   script = home shot:home right w400 shot:right demo:game:Kirby shot:g quit
 * tokens: up down left right x o t s l r st (a press), wN (wait N ms),
 * home (wait for the library), idle (wait for the worker), shot:NAME
 * (log/NAME.bmp of the next frame), demo:WHAT[:GAME] (open a state, like
 * the design's ?demo=), quit.  Absent file = a normal run. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psputility.h>
#include <pspgu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "jobs.h"
#include "model.h"
#include "theme.h"
#include "net.h"
#include "ui.h"
#include "util.h"

PSP_MODULE_INFO("GBAdhocXMB", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-3072);       /* leave room for the net modules and the network dialog */

void ui_init(void);
void ui_draw(void);
void ui_key(int k);
int ui_on_home(void);
void ui_demo(const char *what, const char *arg);

static volatile int g_quit;

static int exit_cb(int a, int b, void *c)
{
   (void)a; (void)b; (void)c;
   g_quit = 1;
   return 0;
}

static int cb_thread(SceSize a, void *b)
{
   int cb;
   (void)a; (void)b;
   cb = sceKernelCreateCallback("xmbexit", exit_cb, NULL);
   sceKernelRegisterExitCallback(cb);
   sceKernelSleepThreadCB();
   return 0;
}

void ui_request_exit(void)
{
   g_quit = 1;
}

/* ---- the firmware's network dialog ---- */

static pspUtilityNetconfData g_nc;
static struct pspUtilityNetconfAdhoc g_nc_adhoc;
static int g_nc_running;
static double g_nc_check;
void ui_toast_ext(const char *msg, int tone);

void ui_netconf_open(void)
{
   if (g_nc_running)
      return;
   net_init();
   memset(&g_nc, 0, sizeof(g_nc));
   memset(&g_nc_adhoc, 0, sizeof(g_nc_adhoc));
   g_nc.base.size = sizeof(g_nc);
   sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &g_nc.base.language);
   g_nc.base.buttonSwap = PSP_UTILITY_ACCEPT_CROSS;
   g_nc.base.graphicsThread = 17;
   g_nc.base.accessThread = 19;
   g_nc.base.fontThread = 18;
   g_nc.base.soundThread = 16;
   g_nc.action = PSP_NETCONF_ACTION_CONNECTAP;
   g_nc.adhocparam = &g_nc_adhoc;
   if (sceUtilityNetconfInitStart(&g_nc) == 0)
   {
      g_nc_running = 1;
      LOG("netconf open");
   }
   else
      LOG("netconf failed to open");
      ui_toast_ext("The network dialog could not open. Set up a connection in the XMB's Network Settings.", 2);

}

static void netconf_update(void)
{
   switch (sceUtilityNetconfGetStatus())
   {
   case PSP_UTILITY_DIALOG_VISIBLE:
      sceUtilityNetconfUpdate(1);
      break;
   case PSP_UTILITY_DIALOG_QUIT:
      sceUtilityNetconfShutdownStart();
      break;
   case PSP_UTILITY_DIALOG_NONE:
   case PSP_UTILITY_DIALOG_FINISHED:
      g_nc_running = 0;
      LOG("netconf closed, result %d", g_nc.base.result);
      g_nc_check = now_s() + 6;
      jobs_net_retry();
      break;
   default:
      break;
   }
}

/* ---- harness ---- */

static char g_script[2048];
static char *g_tok;
static double g_wait_until;
static char g_shot[64];
static double g_tok_t0;

static void bmp_write(const char *path)
{
   int stride, x, y;
   const uint32_t *px = gfx_front_pixels(&stride);
   static unsigned char row[480 * 3];
   unsigned char hdr[54];
   unsigned size = 54 + 480 * 3 * 272;
   SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
   if (fd < 0)
      return;
   memset(hdr, 0, sizeof(hdr));
   hdr[0] = 'B'; hdr[1] = 'M';
   hdr[2] = (unsigned char)size; hdr[3] = (unsigned char)(size >> 8); hdr[4] = (unsigned char)(size >> 16);
   hdr[10] = 54; hdr[14] = 40;
   hdr[18] = 480 & 0xFF; hdr[19] = 480 >> 8;
   hdr[22] = 272 & 0xFF; hdr[23] = 272 >> 8;
   hdr[26] = 1; hdr[28] = 24;
   sceIoWrite(fd, hdr, 54);
   for (y = 271; y >= 0; y--)
   {
      for (x = 0; x < 480; x++)
      {
         uint32_t p = px[y * stride + x];
         row[x * 3 + 0] = (unsigned char)(p >> 16);
         row[x * 3 + 1] = (unsigned char)(p >> 8);
         row[x * 3 + 2] = (unsigned char)p;
      }
      sceIoWrite(fd, row, sizeof(row));
   }
   sceIoClose(fd);
}

static int key_of(const char *t)
{
   static const struct { const char *n; int k; } M[] = {
      { "up", K_UP }, { "down", K_DOWN }, { "left", K_LEFT }, { "right", K_RIGHT }, { "x", K_CROSS }, { "o", K_CIRCLE },
      { "t", K_TRI }, { "s", K_SQ }, { "l", K_L }, { "r", K_R }, { "st", K_START }, { "sel", K_SELECT },
   };
   unsigned i;
   for (i = 0; i < sizeof(M) / sizeof(M[0]); i++)
      if (strcmp(t, M[i].n) == 0) return M[i].k;
   return 0;
}

static void harness_step(void)
{
   char tok[160];
   size_t n;
   if (!g_tok || now_s() < g_wait_until || g_shot[0])
      return;
   while (*g_tok == ' ') g_tok++;
   if (!*g_tok)
   {
      g_tok = NULL;
      return;
   }
   n = strcspn(g_tok, " ");
   if (n >= sizeof(tok)) n = sizeof(tok) - 1;
   memcpy(tok, g_tok, n);
   tok[n] = 0;
   if (strcmp(tok, "home") == 0)
   {
      if (!ui_on_home() && now_s() - g_tok_t0 < 90) return;
      g_wait_until = now_s() + 0.4;
   }
   else if (strcmp(tok, "online") == 0)
   {
      if (jobs_online() != 1 && now_s() - g_tok_t0 < 60) return;
      g_wait_until = now_s() + 0.3;
   }
   else if (strcmp(tok, "idle") == 0)
   {
      if (jobs_busy() && now_s() - g_tok_t0 < 120) return;
      g_wait_until = now_s() + 0.3;
   }
   else if (tok[0] == 'w' && tok[1] >= '0' && tok[1] <= '9')
      g_wait_until = now_s() + atoi(tok + 1) / 1000.0;
   else if (strncmp(tok, "shot:", 5) == 0)
      snprintf(g_shot, sizeof(g_shot), "%s", tok + 5);
   else if (strncmp(tok, "demo:", 5) == 0)
   {
      char *a = strchr(tok + 5, ':');
      if (a) *a++ = 0;
      ui_demo(tok + 5, a);
      g_wait_until = now_s() + 0.9;
   }
   else if (strcmp(tok, "quit") == 0)
      g_quit = 1;
   else
   {
      int k = key_of(tok);
      if (k) { LOG("key %s", tok); ui_key(k); }
      g_wait_until = now_s() + 0.45;
   }
   g_tok += n;
   g_tok_t0 = now_s();
}

/* ---- input ---- */

static void read_pad(void)
{
   static unsigned prev;
   static double held_t[32];
   static double next_t[32];
   SceCtrlData pad;
   unsigned cur, i;
   static const struct { unsigned bit; int key; int rep; } MAP[] = {
      { PSP_CTRL_UP, K_UP, 1 }, { PSP_CTRL_DOWN, K_DOWN, 1 }, { PSP_CTRL_LEFT, K_LEFT, 1 }, { PSP_CTRL_RIGHT, K_RIGHT, 1 },
      { PSP_CTRL_CROSS, K_CROSS, 0 }, { PSP_CTRL_CIRCLE, K_CIRCLE, 0 }, { PSP_CTRL_TRIANGLE, K_TRI, 0 },
      { PSP_CTRL_SQUARE, K_SQ, 0 }, { PSP_CTRL_LTRIGGER, K_L, 1 }, { PSP_CTRL_RTRIGGER, K_R, 1 },
      { PSP_CTRL_START, K_START, 0 }, { PSP_CTRL_SELECT, K_SELECT, 0 },
   };
   if (sceCtrlPeekBufferPositive(&pad, 1) <= 0)
      return;
   cur = pad.Buttons;
   /* the stick works as the D-pad */
   if (pad.Lx < 40) cur |= PSP_CTRL_LEFT;
   if (pad.Lx > 215) cur |= PSP_CTRL_RIGHT;
   if (pad.Ly < 40) cur |= PSP_CTRL_UP;
   if (pad.Ly > 215) cur |= PSP_CTRL_DOWN;
   for (i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++)
   {
      unsigned b = MAP[i].bit;
      if ((cur & b) && !(prev & b))
      {
         ui_key(MAP[i].key);
         held_t[i] = now_s();
         next_t[i] = held_t[i] + 0.35;
      }
      else if ((cur & b) && MAP[i].rep && now_s() >= next_t[i])
      {
         ui_key(MAP[i].key);
         next_t[i] = now_s() + 0.08;
      }
   }
   prev = cur;
}

int main(int argc, char **argv)
{
   char ini[PATHCAP];
   unsigned char *buf;
   size_t n;
   int t;
   double fps_t = 0;
   int frames = 0;
   t = sceKernelCreateThread("xmbcb", cb_thread, 0x11, 0x1000, 0, NULL);
   if (t >= 0)
      sceKernelStartThread(t, 0, NULL);
   /* this app's folder, from argv[0] ("ms0:/PSP/GAME/GBAdhocXMB/EBOOT.PBP") */
   if (argc > 0 && argv[0] && strchr(argv[0], '/'))
   {
      snprintf(g_st.app_dir, sizeof(g_st.app_dir), "%s", argv[0]);
      *strrchr(g_st.app_dir, '/') = 0;
   }
   else
      snprintf(g_st.app_dir, sizeof(g_st.app_dir), "ms0:/PSP/GAME/GBAdhocXMB");
   log_open(g_st.app_dir);
   LOG("GBAdhoc XMB start, app %s", g_st.app_dir);
   path_join(ini, sizeof(ini), g_st.app_dir, "xmbapp-harness.ini");
   if (read_all(ini, &buf, &n) == 0)
   {
      if (ini_get((char *)buf, "script", g_script, sizeof(g_script)) == 0)
      {
         g_tok = g_script;
         gfx_text_log(1);    /* each shot also gets <name>.txt: the text on it */
         LOG("harness script: %s", g_script);
      }
      free(buf);
   }
   prefs_load();
   theme_set(g_prefs.theme);
   sceCtrlSetSamplingCycle(0);
   sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
   gfx_init();
   jobs_start();
   ui_init();
   g_tok_t0 = now_s();
   while (!g_quit)
   {
      if (!g_nc_running)
         read_pad();
      harness_step();
      if (g_nc_check > 0 && now_s() > g_nc_check)
      {
         g_nc_check = 0;
         if (jobs_online() != 1)
            ui_toast_ext("Still offline. Art can be fetched later; entries can be made with the art on the card.", 1);
      }
      gfx_frame_begin(g_th->app_bg[2]);
      ui_draw();
      if (g_nc_running)
      {
         /* the dialog draws over our frame: finish ours first */
         gfx_frame_end_with(netconf_update);
      }
      else
         gfx_frame_end();
      jobs_frame_end();
      if (g_shot[0])
      {
         char p[PATHCAP];
         snprintf(p, sizeof(p), "%s/log/%s.bmp", g_st.app_dir, g_shot);
         bmp_write(p);
         {
            /* and the text on it: x0 y0 x1 y1 colour px text */
            const gfx_trec *r;
            int nr = gfx_text_records(&r), i;
            SceUID fd;
            snprintf(p, sizeof(p), "%s/log/%s.txt", g_st.app_dir, g_shot);
            fd = sceIoOpen(p, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
            if (fd >= 0)
            {
               for (i = 0; i < nr; i++)
               {
                  char ln[120];
                  int k = snprintf(ln, sizeof(ln), "%d %d %d %d %08X %.2f %s\n", r[i].x0, r[i].y0, r[i].x1, r[i].y1,
                                   (unsigned)r[i].c, r[i].px, r[i].s);
                  sceIoWrite(fd, ln, (SceSize)k);
               }
               sceIoClose(fd);
            }
         }
         LOG("shot %s", g_shot);
         g_shot[0] = 0;
      }
      frames++;
      if (now_s() - fps_t >= 5)
      {
         if (fps_t > 0)
            LOG("fps %.1f tiles %d", frames / (now_s() - fps_t), gfx_tiles_used());
         fps_t = now_s();
         frames = 0;
      }
   }
   LOG("exit");
   sceGuTerm();
   sceKernelExitGame();
   return 0;
}
