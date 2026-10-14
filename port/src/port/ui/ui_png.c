// Frosted Mint UI kit (task-26): a small PNG writer for the gallery and
// tests (no zlib in the tree): Paeth-filtered rows, deflate with fixed
// Huffman codes and greedy LZ77 over a hash chain. Good enough for UI
// screenshots (mostly flat colour), deterministic, no dependencies.
#include "port/ui/ui_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct { uint8_t *p; size_t n, cap; uint32_t bits; int nbits; int oom; } Out;

static void put(Out *o, uint8_t b) {
  if (o->oom) return;
  if (o->n == o->cap) {
    size_t cap = o->cap ? o->cap * 2 : 1 << 16;
    if (cap < o->cap) { o->oom = 1; return; }
    uint8_t *p = realloc(o->p, cap);
    if (!p) { o->oom = 1; return; }
    o->p = p;
    o->cap = cap;
  }
  o->p[o->n++] = b;
}

static void bits(Out *o, uint32_t v, int n) {   // LSB first
  o->bits |= v << o->nbits;
  o->nbits += n;
  while (o->nbits >= 8) {
    put(o, (uint8_t)o->bits);
    o->bits >>= 8;
    o->nbits -= 8;
  }
}

static uint32_t rev(uint32_t v, int n) {
  uint32_t r = 0;
  for (int i = 0; i < n; i++) r = r << 1 | ((v >> i) & 1);
  return r;
}

static void lit(Out *o, int c) {   // fixed Huffman literal/length code
  if (c < 144) bits(o, rev(0x30 + c, 8), 8);
  else if (c < 256) bits(o, rev(0x190 + c - 144, 9), 9);
  else if (c < 280) bits(o, rev(c - 256, 7), 7);
  else bits(o, rev(0xC0 + c - 280, 8), 8);
}

static const int k_lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const int k_lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const int k_dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const int k_dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void match(Out *o, int len, int dist) {
  int i = 28;
  while (k_lbase[i] > len) i--;
  lit(o, 257 + i);
  if (k_lext[i]) bits(o, (uint32_t)(len - k_lbase[i]), k_lext[i]);
  int d = 29;
  while (k_dbase[d] > dist) d--;
  bits(o, rev((uint32_t)d, 5), 5);
  if (k_dext[d]) bits(o, (uint32_t)(dist - k_dbase[d]), k_dext[d]);
}

#define HBITS 15
#define WIN 32768

static void deflate(Out *o, const uint8_t *d, size_t n) {
  put(o, 0x78);
  put(o, 0x01);
  bits(o, 1, 1);   // final block
  bits(o, 1, 2);   // fixed Huffman
  int32_t *head = malloc(sizeof(int32_t) << HBITS);
  int32_t *prev = malloc(sizeof(int32_t) * WIN);
  if (!head || !prev) {
    free(head);
    free(prev);
    o->oom = 1;
    return;
  }
  memset(head, 0xff, sizeof(int32_t) << HBITS);
  size_t i = 0;
  while (i < n) {
    int best = 0, bd = 0;
    if (i + 3 <= n) {
      uint32_t h = ((uint32_t)d[i] << 16 | d[i + 1] << 8 | d[i + 2]) * 2654435761u >> (32 - HBITS);
      int32_t p = head[h];
      int chain = 32;
      while (p >= 0 && (size_t)p + WIN > i && chain--) {
        int l = 0, maxl = n - i < 258 ? (int)(n - i) : 258;
        while (l < maxl && d[p + l] == d[i + l]) l++;
        if (l > best) {
          best = l;
          bd = (int)(i - p);
          if (l == maxl) break;
        }
        p = prev[p % WIN];
      }
      prev[i % WIN] = head[h];
      head[h] = (int32_t)i;
    }
    if (best >= 3) {
      match(o, best, bd);
      for (int k = 1; k < best && i + k + 3 <= n; k++) {
        size_t j = i + k;
        uint32_t h = ((uint32_t)d[j] << 16 | d[j + 1] << 8 | d[j + 2]) * 2654435761u >> (32 - HBITS);
        prev[j % WIN] = head[h];
        head[h] = (int32_t)j;
      }
      i += best;
    } else {
      lit(o, d[i++]);
    }
  }
  lit(o, 256);
  if (o->nbits) bits(o, 0, 8 - o->nbits);
  uint32_t a = 1, b = 0;
  for (size_t k = 0; k < n; k++) {
    a = (a + d[k]) % 65521;
    b = (b + a) % 65521;
  }
  uint32_t ad = b << 16 | a;
  put(o, (uint8_t)(ad >> 24));
  put(o, (uint8_t)(ad >> 16));
  put(o, (uint8_t)(ad >> 8));
  put(o, (uint8_t)ad);
  free(head);
  free(prev);
}

static uint32_t crc(const uint8_t *p, size_t n, uint32_t c) {
  static uint32_t t[256];
  if (!t[1])
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t v = i;
      for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
      t[i] = v;
    }
  c = ~c;
  for (size_t i = 0; i < n; i++) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
  return ~c;
}

static void chunk(FILE *f, const char *type, const uint8_t *p, size_t n) {
  uint8_t h[8] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n,
                  (uint8_t)type[0], (uint8_t)type[1], (uint8_t)type[2], (uint8_t)type[3]};
  fwrite(h, 1, 8, f);
  if (n) fwrite(p, 1, n, f);
  uint32_t c = crc(h + 4, 4, 0);
  c = crc(p, n, c);
  uint8_t e[4] = {(uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
  fwrite(e, 1, 4, f);
}

static int paeth(int a, int b, int c) {
  int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

int ui_png_write(const char *path, const uint32_t *argb, int w, int h, int pitch_px) {
  if (!path || !argb || w <= 0 || h <= 0 || pitch_px < w ||
      (size_t)w > (SIZE_MAX - 1) / 3) return 0;
  size_t rgb = (size_t)w * 3, row = rgb + 1;
  if ((size_t)h > SIZE_MAX / row) return 0;
  uint8_t *raw = malloc(row * (size_t)h), *cur = malloc(rgb), *up = calloc(rgb, 1);
  if (!raw || !cur || !up) {
    free(raw);
    free(cur);
    free(up);
    return 0;
  }
  for (int y = 0; y < h; y++) {
    const uint32_t *s = argb + (size_t)y * pitch_px;
    for (int x = 0; x < w; x++) {
      cur[x * 3] = (uint8_t)(s[x] >> 16);
      cur[x * 3 + 1] = (uint8_t)(s[x] >> 8);
      cur[x * 3 + 2] = (uint8_t)s[x];
    }
    uint8_t *o = raw + row * y;
    o[0] = 4;   // Paeth
    for (int x = 0; x < w * 3; x++) {
      int a = x >= 3 ? cur[x - 3] : 0, b = up[x], c = x >= 3 ? up[x - 3] : 0;
      o[1 + x] = (uint8_t)(cur[x] - paeth(a, b, c));
    }
    memcpy(up, cur, (size_t)w * 3);
  }
  Out z = {0};
  deflate(&z, raw, row * (size_t)h);
  if (z.oom || z.n > UINT32_MAX) {
    free(z.p);
    free(raw);
    free(cur);
    free(up);
    return 0;
  }
  FILE *f = fopen(path, "wb");
  int ok = f != NULL;
  if (f) {
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                        (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0};
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z.p, z.n);
    chunk(f, "IEND", NULL, 0);
    ok = !ferror(f);
    if (fclose(f) != 0) ok = 0;
    if (!ok) remove(path);  // do not leave an invalid partial PNG
  }
  free(z.p);
  free(raw);
  free(cur);
  free(up);
  return ok;
}
