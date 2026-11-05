/* test_obs.c -- relay_obs: ping, loss, rates, nickname and the bounded table,
 * on synthetic GBR1 + netdrv frames (virtual time).  GBAdhoc XMB's own test.
 *   cc -I.. -o test_obs test_obs.c ../relay_obs.c && ./test_obs */
#include "relay_obs.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* GBR1 DATA carrying a netdrv frame of `type` src->dst seq/ack, payload n */
static size_t frame(uint8_t *b, int type, int src, int dst, unsigned seq, unsigned ack,
                    const void *pl, unsigned n)
{
   uint8_t *f = b + RL_HDR;
   rl_hdr(b, RL_T_DATA, src, dst, 0x1122334455667788ull);
   memset(f, 0, 24);
   f[0] = 0x31; f[1] = 0x4E; f[2] = 0x50; f[3] = 0x47; f[4] = 2;
   f[5] = (uint8_t)type; f[6] = (uint8_t)src; f[7] = (uint8_t)dst;
   f[8] = (uint8_t)seq; f[9] = (uint8_t)(seq >> 8);
   f[10] = (uint8_t)ack; f[11] = (uint8_t)(ack >> 8);
   f[12] = (uint8_t)n; f[13] = (uint8_t)(n >> 8);
   if (n)
      memcpy(f + 24, pl, n);
   return RL_HDR + 24 + n;
}

int main(void)
{
   static relay_obs o;
   uint8_t b[RL_MAX_DGRAM];
   rl_addr A = { 0x0100007f, 5000 }, B = { 0x0100007f, 5001 };
   uint64_t t = 1000000;
   const obs_ent *e;
   size_t n;
   int i, retx = 0;
   uint8_t join[24 + 16 + 4];

   relay_obs_init(&o);
   /* nothing known before anything was heard */
   CHECK(relay_obs_find(&o, &B) == NULL, "empty table");
   n = frame(b, 0, 0, 1, 0, 0, "x", 1);
   relay_obs_tx(&o, &B, b, n, t);
   CHECK(relay_obs_find(&o, &B) == NULL, "a send never creates an entry");

   /* B joins with a nickname */
   memset(join, 0, sizeof(join));
   memcpy(join, "gpSP v1.0", 9);
   memcpy(join + 24, "Mara", 4);
   n = frame(b, 4, 0xFE, 0xFF, 0, 0, join, sizeof(join));
   relay_obs_rx(&o, &B, b, n, t);
   e = relay_obs_find(&o, &B);
   CHECK(e && !strcmp(e->nick, "Mara"), "nick %s", e ? e->nick : "-");
   CHECK(relay_obs_ping_ms(e) == -1, "no ping yet");

   /* the relay forwards A's DATA seq s to B every 16 ms; B acks each 30 ms later */
   for (i = 0; i < 200; i++)
   {
      n = frame(b, 0, 0, 1, (unsigned)i, 0, "abcd", 4);
      relay_obs_tx(&o, &B, b, n, t + (uint64_t)i * 16000);
      n = frame(b, 1, 1, 0, 0, (unsigned)i + 1, NULL, 0);
      relay_obs_rx(&o, &B, b, n, t + (uint64_t)i * 16000 + 30000);
   }
   e = relay_obs_find(&o, &B);
   CHECK(relay_obs_ping_ms(e) == 30, "ping %d", relay_obs_ping_ms(e));

   /* Karn: a resent seq gives no sample even when its ack comes */
   {
      uint32_t before = e->rtt_samples;
      n = frame(b, 0, 0, 1, 500, 0, "abcd", 4);
      relay_obs_tx(&o, &B, b, n, t + 4000000);
      relay_obs_tx(&o, &B, b, n, t + 4200000);
      n = frame(b, 1, 1, 0, 0, 501, NULL, 0);
      relay_obs_rx(&o, &B, b, n, t + 4210000);
      CHECK(e->rtt_samples == before, "retransmit sampled");
   }

   /* B sends 200 reliable frames, 10 of them twice: 10/210 = 48 per mille */
   t += 5000000;
   for (i = 0; i < 200; i++)
   {
      n = frame(b, 0, 1, 0, (unsigned)(1000 + i), 0, "abcd", 4);
      relay_obs_rx(&o, &B, b, n, t + (uint64_t)i * 20000);
      if (i % 20 == 5)
      {
         relay_obs_rx(&o, &B, b, n, t + (uint64_t)i * 20000 + 1000);
         retx++;
      }
   }
   e = relay_obs_find(&o, &B);
   CHECK(e->rx_retx == (uint32_t)retx, "retx %u want %d", e->rx_retx, retx);
   CHECK(relay_obs_loss_permille(e, t + 4000000) >= 40 && relay_obs_loss_permille(e, t + 4000000) <= 60,
         "loss %d", relay_obs_loss_permille(e, t + 4000000));
   CHECK(relay_obs_pps_in(e, t + 4000000) > 20, "pps in %d", relay_obs_pps_in(e, t + 4000000));
   /* figures go stale after 10 s of silence */
   CHECK(relay_obs_loss_permille(e, t + 30000000) == -1, "stale loss %d", relay_obs_loss_permille(e, t + 30000000));
   CHECK(relay_obs_pps_in(e, t + 60000000) == 0, "stale pps %d", relay_obs_pps_in(e, t + 60000000));

   /* seq wrap */
   {
      static relay_obs o2;
      relay_obs_init(&o2);
      for (i = 0; i < 300; i++)
      {
         n = frame(b, 0, 1, 0, (unsigned)(65400 + i) & 0xFFFF, 0, "a", 1);
         relay_obs_rx(&o2, &A, b, n, t + (uint64_t)i * 1000);
      }
      e = relay_obs_find(&o2, &A);
      CHECK(e && e->rx_retx == 0, "wrap counted as retx: %u", e ? e->rx_retx : 0);
   }

   /* a PATH DIRECT marks the pair direct; relayed DATA clears it */
   {
      uint8_t p[RL_HDR + 2];
      rl_hdr(p, RL_T_PATH, 1, RL_SLOT_NONE, 1);
      p[RL_HDR] = 0;
      p[RL_HDR + 1] = RL_PATH_DIRECT;
      relay_obs_rx(&o, &B, p, sizeof(p), t + 7000000);
      CHECK(relay_obs_find(&o, &B)->direct == 1, "direct");
      n = frame(b, 1, 1, 0, 0, 0, NULL, 0);
      relay_obs_rx(&o, &B, b, n, t + 7100000);
      CHECK(relay_obs_find(&o, &B)->direct == 0, "back on relay");
   }

   /* the table is bounded: a thousand sources, OBS_N entries, the newest kept */
   for (i = 0; i < 1000; i++)
   {
      rl_addr x = { (uint32_t)(0x0A000000u + (unsigned)i), (uint16_t)(1000 + i) };
      n = frame(b, 8, 1, 0, 0, 0, NULL, 0);
      relay_obs_rx(&o, &x, b, n, t + 8000000 + (uint64_t)i);
   }
   {
      rl_addr last = { 0x0A000000u + 999u, (uint16_t)1999 };
      int used = 0;
      for (i = 0; i < OBS_N; i++)
         used += o.e[i].used;
      CHECK(used == OBS_N, "used %d", used);
      CHECK(relay_obs_find(&o, &last) != NULL, "newest kept");
   }
   /* garbage never takes an entry; truncated / mislabelled frames do not crash */
   {
      static relay_obs o3;
      rl_addr g = { 0x01020304, 9 };
      relay_obs_init(&o3);
      memset(b, 0xA5, sizeof(b));
      for (i = 0; i <= RL_MAX_DGRAM; i++)
         relay_obs_rx(&o3, &g, b, (size_t)i, t);
      CHECK(relay_obs_find(&o3, &g) == NULL, "garbage made an entry");
      n = frame(b, 0, 1, 0, 1, 0, "abcd", 4);
      for (i = 0; i <= (int)n + 8; i++)
         relay_obs_rx(&o3, &g, b, (size_t)i, t);
   }

   printf("%s: relay_obs\n", fails ? "FAILED" : "PASS");
   return fails ? 1 : 0;
}
