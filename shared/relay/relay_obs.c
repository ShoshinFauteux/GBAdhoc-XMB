/* relay_obs.c -- per-console figures for a relay host's status screen.
 * See relay_obs.h: reads headers, changes nothing, bounded memory. */
#include "relay_obs.h"

#include <string.h>

#define WIN_US 5000000ull
#define ND_HDR 24

/* netdrv frame types whose ack field is meaningful (netdrv_wire.h) */
enum { NDT_DATA = 0, NDT_ACK = 1, NDT_JOIN = 4, NDT_PING = 8 };

void relay_obs_init(relay_obs *o)
{
   memset(o, 0, sizeof(*o));
}

static int same(const rl_addr *a, const rl_addr *b)
{
   return a->ip_be == b->ip_be && a->port == b->port;
}

const obs_ent *relay_obs_find(const relay_obs *o, const rl_addr *a)
{
   int i;
   for (i = 0; i < OBS_N; i++)
      if (o->e[i].used && same(&o->e[i].addr, a))
         return &o->e[i];
   return NULL;
}

static obs_ent *get(relay_obs *o, const rl_addr *a, uint64_t now)
{
   int i;
   obs_ent *old = NULL;
   for (i = 0; i < OBS_N; i++)
   {
      obs_ent *e = &o->e[i];
      if (e->used && same(&e->addr, a))
         return e;
      if (!e->used)
      {
         if (!old || old->used)
            old = e;
      }
      else if (!old || (old->used && e->last_us < old->last_us))
         old = e;
   }
   memset(old, 0, sizeof(*old));
   old->used = 1;
   old->addr = *a;
   old->first_us = old->win_us = now;
   old->p_len_us = 0;
   return old;
}

static void roll(obs_ent *e, uint64_t now)
{
   uint64_t age = now - e->win_us;
   if (age < WIN_US)
      return;
   if (age >= 2 * WIN_US)
   {
      e->p_rx = e->p_tx = e->p_data = e->p_retx = 0;
      e->p_len_us = (uint32_t)WIN_US;
   }
   else
   {
      e->p_rx = e->w_rx;
      e->p_tx = e->w_tx;
      e->p_data = e->w_data;
      e->p_retx = e->w_retx;
      e->p_len_us = (uint32_t)age;
   }
   e->w_rx = e->w_tx = e->w_data = e->w_retx = 0;
   e->win_us = now;
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

/* the netdrv frame inside a GBR1 DATA, or NULL */
static const uint8_t *nd_frame(const uint8_t *b, size_t len)
{
   const uint8_t *f;
   size_t n;
   if (len < RL_HDR + ND_HDR || rl_check(b, (int)len) != 0 || b[5] != RL_T_DATA)
      return NULL;
   f = b + RL_HDR;
   n = len - RL_HDR;
   if (f[0] != 0x31 || f[1] != 0x4E || f[2] != 0x50 || f[3] != 0x47 || f[4] != 2)
      return NULL;
   if (ND_HDR + le16(f + 12) != n)
      return NULL;
   return f;
}

/* 1 = a sequence number this stream already carried (a retransmit) */
static int seq_seen(obs_ent *e, uint8_t src, uint8_t dst, uint16_t seq)
{
   int i;
   obs_stream *s = NULL, *fr = NULL;
   int16_t d;
   for (i = 0; i < OBS_STREAMS; i++)
   {
      if (e->st[i].used && e->st[i].src == src && e->st[i].dst == dst)
      {
         s = &e->st[i];
         break;
      }
      if (!e->st[i].used && !fr)
         fr = &e->st[i];
   }
   if (!s)
   {
      s = fr ? fr : &e->st[0];
      s->used = 1;
      s->src = src;
      s->dst = dst;
      s->hi = seq;
      s->seen = 1;
      return 0;
   }
   d = (int16_t)(uint16_t)(seq - s->hi);
   if (d > 0)
   {
      s->seen = d >= 64 ? 0 : s->seen << d;
      s->seen |= 1;
      s->hi = seq;
      return 0;
   }
   if (-d >= 64)
      return 1;
   if ((s->seen >> -d) & 1)
      return 1;
   s->seen |= 1ull << -d;
   return 0;
}

void relay_obs_rx(relay_obs *o, const rl_addr *from, const void *buf, size_t len, uint64_t now)
{
   const uint8_t *b = (const uint8_t *)buf, *f;
   obs_ent *e;
   if (len < RL_HDR || rl_check(b, (int)len) != 0)
      return;          /* garbage never takes a table entry */
   e = get(o, from, now);
   roll(e, now);
   e->last_us = now;
   e->rx++;
   e->w_rx++;
   if (b[5] == RL_T_PATH && len >= RL_HDR + 2)
      e->direct = b[RL_HDR + 1] == RL_PATH_DIRECT;
   f = nd_frame(b, len);
   if (!f)
      return;
   e->direct = 0;      /* DATA through the relay: this pair is on the relay */
   if (f[5] == NDT_DATA)
   {
      e->rx_data++;
      e->w_data++;
      if (seq_seen(e, f[6], f[7], (uint16_t)le16(f + 8)))
      {
         e->rx_retx++;
         e->w_retx++;
      }
   }
   if (f[5] == NDT_JOIN && le16(f + 12) >= 24 + 16)
   {
      int i;
      for (i = 0; i < 16; i++)
      {
         char c = (char)f[ND_HDR + 24 + i];
         if (!c)
            break;
         e->nick[i] = (c >= 32 && c < 127) ? c : '?';
      }
      e->nick[i] = '\0';
   }
   if ((f[5] == NDT_DATA || f[5] == NDT_ACK || f[5] == NDT_PING) && f[6] < 5 && f[7] < 5)
   {
      /* this frame from X (src B) acks what A (its dst) sent to it */
      uint16_t ack = (uint16_t)le16(f + 10);
      uint64_t best_t = 0;
      int i, have = 0;
      for (i = 0; i < OBS_PEND; i++)
      {
         obs_pend *p = &e->pend[i];
         if (!p->used || p->src != f[7] || p->dst != f[6])
            continue;
         if ((int16_t)(uint16_t)(ack - p->seq) <= 0)
            continue;
         if (!p->retx && (!have || p->t_us > best_t))
         {
            best_t = p->t_us;
            have = 1;
         }
         p->used = 0;
      }
      if (have && now >= best_t)
      {
         uint32_t s = (uint32_t)(now - best_t);
         e->srtt_us = e->rtt_samples ? (uint32_t)((7ull * e->srtt_us + s) / 8) : s;
         e->rtt_samples++;
      }
   }
}

void relay_obs_tx(relay_obs *o, const rl_addr *to, const void *buf, size_t len, uint64_t now)
{
   const uint8_t *b = (const uint8_t *)buf, *f;
   obs_ent *e;
   int i;
   obs_pend *slot = NULL;
   uint16_t seq;
   /* only consoles we heard from get an entry: a reply to a stranger
    * (COOKIE, ERROR) never takes one */
   for (i = 0; i < OBS_N; i++)
      if (o->e[i].used && same(&o->e[i].addr, to))
         break;
   if (i == OBS_N)
      return;
   e = &o->e[i];
   roll(e, now);
   e->tx++;
   e->w_tx++;
   f = nd_frame(b, len);
   if (!f || f[5] != NDT_DATA || f[6] >= 5 || f[7] >= 5)
      return;
   seq = (uint16_t)le16(f + 8);
   for (i = 0; i < OBS_PEND; i++)
   {
      obs_pend *p = &e->pend[i];
      if (p->used && p->src == f[6] && p->dst == f[7] && p->seq == seq)
      {
         p->retx = 1;          /* Karn: no sample from a resent seq */
         return;
      }
      if (!p->used)
      {
         if (!slot || slot->used)
            slot = p;
      }
      else if (!slot || (slot->used && p->t_us < slot->t_us))
         slot = p;
   }
   slot->used = 1;
   slot->retx = 0;
   slot->src = f[6];
   slot->dst = f[7];
   slot->seq = seq;
   slot->t_us = now;
}

int relay_obs_ping_ms(const obs_ent *e)
{
   if (!e || !e->rtt_samples)
      return -1;
   return (int)((e->srtt_us + 500) / 1000);
}

static uint64_t span(const obs_ent *e, uint64_t now)
{
   uint64_t s = e->p_len_us + (now - e->win_us);
   if (now - e->first_us < s)
      s = now - e->first_us;
   return s < 1000000ull ? 1000000ull : s;
}

int relay_obs_loss_permille(const obs_ent *e, uint64_t now)
{
   uint32_t d, r;
   (void)now;
   if (!e)
      return -1;
   d = e->p_data + e->w_data;
   r = e->p_retx + e->w_retx;
   if (now - e->win_us >= 2 * WIN_US)
      return -1;
   if (!d)
      return -1;
   return (int)((r * 1000ull + d / 2) / d);
}

int relay_obs_pps_in(const obs_ent *e, uint64_t now)
{
   if (!e || now - e->win_us >= 2 * WIN_US)
      return 0;
   return (int)((e->p_rx + e->w_rx) * 1000000ull / span(e, now));
}

int relay_obs_pps_out(const obs_ent *e, uint64_t now)
{
   if (!e || now - e->win_us >= 2 * WIN_US)
      return 0;
   return (int)((e->p_tx + e->w_tx) * 1000000ull / span(e, now));
}
