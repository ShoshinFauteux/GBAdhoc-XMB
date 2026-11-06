/* jobs.h -- the worker thread: everything slow (Memory Stick, decoding,
 * network, writing entries) runs here while the UI draws at 60 fps.
 *
 * One task at a time is the user's (scan, get art, make, remove, a batch):
 * g_task, whose steps the open dialog shows.  Behind it, at low priority,
 * the worker composes icons and loads pictures the screen asks for.
 * Buffers it replaces are retired, and freed by the UI thread after the
 * frame that may still draw them (jobs_frame_end). */
#ifndef XMBAPP_JOBS_H
#define XMBAPP_JOBS_H

#include "model.h"

enum { STEP_PEND = 0, STEP_OK, STEP_BAD, STEP_WARN };
enum { TASK_NONE = 0, TASK_SCAN, TASK_FETCH, TASK_MAKE, TASK_REMOVE, TASK_BATCH_MAKE, TASK_BATCH_ART, TASK_CONNECT };
enum { RES_OK = 0, RES_FAIL, RES_NOSPACE, RES_OFFLINE, RES_RATE, RES_NOTFOUND, RES_CANCEL };
enum { BI_WAIT = 0, BI_PEND, BI_OK, BI_WARN, BI_BAD, BI_SKIP };

#define MAX_STEPS 14

typedef struct
{
   char msg[160];
   int st;
} step_t;

typedef struct
{
   int game;
   int st;               /* BI_* */
   char msg[64];
} batch_item;

typedef struct
{
   volatile int kind;    /* TASK_*, NONE when idle */
   volatile int done;
   volatile int cancel;  /* the user asked to stop (after the current item) */
   int game;
   int result;           /* RES_* */
   int replace;          /* make: an entry exists and may be rewritten */
   step_t steps[MAX_STEPS];
   int nsteps;
   /* scan progress */
   char msg[128], sub[160];
   float frac;
   /* batch */
   batch_item items[MAX_GAMES];
   int nitems, cur;
   /* fetch */
   int nbox_found, hero_found;
} task_t;

extern task_t g_task;

void jobs_start(void);
/* user tasks (ignored while one runs) */
int jobs_busy(void);
void jobs_scan(void);
void jobs_fetch(int game);
void jobs_make(int game, int replace);
void jobs_remove(int game);
void jobs_batch(const int *games, int n, int make);
void jobs_ack(void);              /* the dialog closed: g_task back to idle */

/* screen-side caches; NULL while not ready (the request is queued) */
const img_slot *jobs_icon(int game);           /* ICON0 144x80 */
const img_slot *jobs_pic(int game);            /* PIC1 480x272 (NULL if none chosen) */
/* a thumbnail: NULL while queued, state IMG_FAIL (px NULL) when unreadable */
const img_slot *jobs_thumb(const char *path, int sect, int w, int h, int cover);
void jobs_frame_end(void);        /* free retired buffers (UI thread) */

/* network status for the top bar */
int jobs_online(void);
void jobs_net_retry(void);
void jobs_net_info(char *ssid, int cap);
/* attempts the network thread has finished (a request waits for it to move) */
int jobs_net_attempts(void);
/* 1 while an attempt runs or a retry waits to start */
int jobs_net_busy(void);
/* why the last attempt failed, with the firmware code ("" when online) */
void jobs_net_error(char *out, int cap);

/* lock around reading g_task text from the UI */
void task_lock(void);
void task_unlock(void);

#endif
