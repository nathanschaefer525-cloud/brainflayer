/* Copyright (c) 2015 Nicolas Courtois, Guangyan Song, Ryan Castellucci, All Rights Reserved */
#include "ec_pubkey_fast.h"

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <endian.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "secp256k1/src/libsecp256k1-config.h"
#include "secp256k1/include/secp256k1.h"

#include "secp256k1/src/util.h"
#include "secp256k1/src/num_impl.h"
#include "secp256k1/src/field_impl.h"
#include "secp256k1/src/field_10x26_impl.h"
#include "secp256k1/src/scalar_impl.h"
#include "secp256k1/src/group_impl.h"
#include "secp256k1/src/ecmult_gen_impl.h"
#include "secp256k1/src/ecmult.h"
#include "secp256k1/src/eckey_impl.h"

void secp256k1_ecmult(const secp256k1_ecmult_context_t *ctx, secp256k1_gej_t *r, const secp256k1_gej_t *a, const secp256k1_scalar_t *na, const secp256k1_scalar_t *ng) {
  fprintf(stderr, "there is no secp256k1_ecmult %p %p %p %p %p\n", (void*)ctx, (void*)r, (void*)a, (void*)na, (void*)ng);
  abort();
}

static int secp256k1_eckey_pubkey_parse(secp256k1_ge_t *elem, const unsigned char *pub, int size);

#include "mmapf.h"

#undef ASSERT

#define READBIT(A, B) ((A >> (B & 7)) & 1)
#define SETBIT(T, B, V) (T = V ? T | (1<<B) : T & ~(1<<B))
int n_windows = 0;
int n_values;
secp256k1_gej_t nums_gej;
secp256k1_ge_t *prec;
int remmining = 0;
int WINDOW_SIZE = 0;
size_t MMAP_SIZE;
mmapf_ctx prec_mmapf;

static unsigned char hashtab[][33] = {
  {  4, 0x23, 0x2a, 0x4d, 0x5d, 0x6a, 0x30, 0x9e, 0xc3, 0xb2, 0x95, 0xb6, 0x00, 0x75, 0x69, 0xc4, 0x42, 0x6e, 0xc2, 0x9f, 0xd4, 0x47, 0xd2, 0xc6, 0x54, 0x43, 0x30, 0x43, 0x9d, 0xa8, 0xff, 0xe3, 0x8b },
  {  5, 0xf9, 0xce, 0x9d, 0x26, 0xa3, 0x12, 0x58, 0x2f, 0x84, 0x46, 0x05, 0xee, 0x8d, 0xd0, 0xf8, 0x3b, 0x9f, 0xff, 0x2b, 0x01, 0x94, 0xcb, 0x30, 0x69, 0x6b, 0xc5, 0x1e, 0x30, 0x5e, 0x2e, 0x95, 0x14 },
  {  6, 0xda, 0xdf, 0x6a, 0x0c, 0x03, 0x5d, 0x5b, 0xc8, 0x23, 0xe0, 0xd7, 0xed, 0xe6, 0x6f, 0x5e, 0xc1, 0x3f, 0x94, 0x1f, 0x7e, 0x46, 0xe5, 0x6d, 0x36, 0x18, 0xa8, 0xde, 0x2c, 0xe1, 0xfe, 0x5d, 0x4c },
  {  7, 0x0d, 0xa7, 0xeb, 0xd9, 0x18, 0x53, 0x0b, 0xa2, 0x11, 0x31, 0x1c, 0x42, 0xa6, 0xf0, 0x2a, 0x95, 0x84, 0x55, 0x45, 0x75, 0x6a, 0x8f, 0x52, 0xa6, 0x7f, 0x3c, 0x8b, 0xd9, 0x48, 0x4c, 0x94, 0x4c },
  {  8, 0x02, 0x25, 0xcc, 0x2b, 0x15, 0xfc, 0x19, 0xe1, 0x8a, 0xc4, 0xce, 0x05, 0x0d, 0xe3, 0xba, 0x9e, 0x94, 0xbd, 0x83, 0x8e, 0xfd, 0x52, 0xf5, 0x13, 0x09, 0x37, 0x96, 0x4c, 0xd8, 0xf1, 0xac, 0xdf },
  {  9, 0xb7, 0x89, 0x12, 0x0e, 0xd7, 0xe1, 0x12, 0xdf, 0xc3, 0xbe, 0x2b, 0x3c, 0x86, 0x45, 0xa0, 0x9c, 0x41, 0x81, 0x9a, 0xbc, 0xe4, 0x8d, 0xfd, 0xd1, 0x44, 0xd8, 0xae, 0xe7, 0xe5, 0x9c, 0x25, 0x55 },
  { 10, 0x5a, 0xaf, 0x04, 0x2b, 0xed, 0x6d, 0x33, 0x3b, 0x83, 0xec, 0x75, 0x2f, 0x3f, 0x87, 0x00, 0x4d, 0xde, 0x36, 0xed, 0xa0, 0x33, 0xc5, 0x2b, 0x68, 0xb7, 0x9f, 0x92, 0xe1, 0x59, 0xc7, 0x89, 0xb5 },
  { 11, 0x16, 0xc7, 0xf6, 0x32, 0x5e, 0x22, 0x1e, 0xe5, 0xe4, 0xcd, 0xa8, 0x99, 0xcd, 0x40, 0xfd, 0x7b, 0x7d, 0x9e, 0x84, 0x78, 0x96, 0x52, 0xb5, 0xec, 0x28, 0x88, 0x1e, 0x92, 0x1c, 0x07, 0x8d, 0xc5 },
  { 12, 0xa2, 0xe3, 0xed, 0xa6, 0xe2, 0x0b, 0x24, 0xab, 0x75, 0xef, 0x02, 0xb1, 0xec, 0x12, 0x43, 0x4b, 0x4a, 0x2b, 0x9e, 0xcb, 0x4a, 0xdb, 0x91, 0x1b, 0x90, 0xef, 0x1e, 0x99, 0x99, 0x26, 0x26, 0x07 },
  { 13, 0xf3, 0xc5, 0x24, 0x2b, 0xf2, 0x9f, 0x43, 0x9c, 0x3c, 0xb6, 0x83, 0x1b, 0x84, 0x74, 0x7e, 0xc5, 0xd0, 0xf0, 0xe9, 0x53, 0xd2, 0x4a, 0x66, 0x7b, 0xd4, 0x58, 0x0b, 0xb5, 0xb5, 0xd4, 0x8f, 0x82 },
  { 14, 0x3c, 0x13, 0x74, 0xac, 0x30, 0xd4, 0x80, 0xf2, 0x77, 0xa5, 0xe0, 0x61, 0x72, 0x70, 0x83, 0x6b, 0x04, 0xd1, 0x50, 0x98, 0xa6, 0xad, 0xae, 0x26, 0xb1, 0x83, 0x98, 0x1b, 0xe2, 0xe9, 0x93, 0x3b },
  { 15, 0xc8, 0x8c, 0x8b, 0x2f, 0xb9, 0xdd, 0xcc, 0xf9, 0x16, 0xff, 0xc7, 0x71, 0x75, 0x3f, 0xd8, 0xba, 0x4d, 0xfa, 0xee, 0x2f, 0xa4, 0x92, 0x22, 0xd7, 0xc3, 0x51, 0x8c, 0xc5, 0x7b, 0x68, 0x5e, 0x69 },
  { 16, 0xaf, 0xd1, 0x00, 0x13, 0xc9, 0xcb, 0x63, 0x67, 0x0a, 0xb0, 0xd7, 0x19, 0xd8, 0x95, 0xd8, 0xda, 0xcd, 0x9f, 0xd2, 0xea, 0xad, 0x0b, 0x3f, 0xbb, 0x96, 0xe5, 0x58, 0x69, 0x89, 0x29, 0x87, 0xa4 },
  { 18, 0x1a, 0xda, 0x03, 0xbc, 0xb9, 0x55, 0x18, 0x77, 0x0b, 0x26, 0x38, 0xa0, 0x9b, 0xc2, 0x7f, 0xbe, 0x43, 0xee, 0x13, 0x75, 0xa8, 0x20, 0x6d, 0xd2, 0xf9, 0xa0, 0x0d, 0x99, 0x63, 0x49, 0xae, 0x64 },
  { 19, 0x30, 0xc5, 0x1d, 0xf4, 0x2d, 0xab, 0x7a, 0x2f, 0x08, 0x05, 0xd8, 0x15, 0xfe, 0x52, 0xd9, 0x66, 0x75, 0x1b, 0x65, 0x35, 0x69, 0xb6, 0x40, 0x43, 0x27, 0xb4, 0xf2, 0x87, 0xf3, 0xd0, 0x27, 0x5f },
  { 20, 0xa1, 0xd8, 0x90, 0xb4, 0x0d, 0x32, 0xe3, 0x06, 0xf2, 0x15, 0x1c, 0xd1, 0x5c, 0x74, 0x1e, 0x6d, 0xeb, 0x88, 0xad, 0x42, 0xdf, 0xb1, 0x6a, 0xca, 0x96, 0x35, 0xc8, 0xbe, 0xbd, 0xd3, 0x8e, 0xfc },
  { 22, 0x75, 0xba, 0xa1, 0x0e, 0x55, 0xab, 0x18, 0xf5, 0x10, 0xff, 0x47, 0x1e, 0xec, 0xa2, 0x8d, 0xe1, 0xd4, 0xc5, 0x1e, 0x03, 0x4d, 0x7b, 0x3c, 0x34, 0xf8, 0x9a, 0xbb, 0x20, 0xa4, 0xaa, 0x4b, 0x0d },
  { 24, 0xe9, 0x76, 0xb2, 0x22, 0x82, 0x87, 0x2b, 0xd4, 0x8c, 0xf2, 0xbd, 0xdf, 0xb9, 0x43, 0xe8, 0xef, 0xfe, 0x0e, 0x8e, 0xa6, 0xee, 0x56, 0xa3, 0xeb, 0x06, 0x46, 0xdc, 0x1c, 0x10, 0xec, 0xed, 0x05 },
  { 26, 0xef, 0x40, 0xa9, 0x32, 0x9b, 0xd8, 0xd3, 0xb9, 0x6a, 0xda, 0x3b, 0xa0, 0xd3, 0xfc, 0xb7, 0xaf, 0xf5, 0x59, 0x4f, 0x8f, 0xcb, 0x7a, 0xec, 0x80, 0xd1, 0x01, 0x6f, 0x26, 0x2b, 0xbf, 0xf7, 0xaa },
};

int secp256k1_ec_pubkey_precomp_table_save(int window_size, unsigned char *filename) {
  int fd, ret;
  size_t records;
  FILE *dest;

  if ((ret = secp256k1_ec_pubkey_precomp_table(window_size, NULL)) < 0)
    return ret;

  if ((fd = open(filename, O_RDWR | O_CREAT | O_EXCL, 0660)) < 0)
    return fd;

  records = n_windows*n_values;
  dest = fdopen(fd, "w");
  if (fwrite(prec, sizeof(secp256k1_ge_t), n_windows*n_values, dest) != records)
    return -1;

  return 0;
}

int secp256k1_ec_pubkey_precomp_table(int window_size, unsigned char *filename) {
  int ret;
  struct stat sb;
  size_t prec_sz;
  secp256k1_gej_t gj; // base point in jacobian coordinates
  secp256k1_gej_t *table;

  if (filename) {
    if (stat(filename, &sb) == 0) {
      if (!S_ISREG(sb.st_mode))
        return -100;
    } else {
      return -101;
    }
  }

  // try to find a window size that matched the file size
  for (;;) {
    WINDOW_SIZE = window_size;
    n_values = 1 << window_size;
    if (256 % window_size == 0) {
      n_windows = (256 / window_size);
    } else {
      n_windows = (256 / window_size) + 1;
    }
    remmining = 256 % window_size;
    prec_sz = n_windows*n_values*sizeof(secp256k1_ge_t);
    if (!filename || sb.st_size <= prec_sz)
      break;
    ++window_size;
  }

  if ((ret = mmapf(&prec_mmapf, filename, prec_sz, MMAPF_RNDRD)) != MMAPF_OKAY) {
    fprintf(stderr, "failed to open ecmult table '%s': %s\n", filename, mmapf_strerror(ret));
    exit(1);
  } else if (prec_mmapf.mem == NULL) {
    fprintf(stderr, "got NULL pointer from mmapf\n");
    exit(1);
  }
  prec = prec_mmapf.mem;

  if (filename) { return 0; }

  table = malloc(n_values*sizeof(secp256k1_gej_t));

  secp256k1_gej_set_ge(&gj, &secp256k1_ge_const_g);

  //fprintf(stderr, "%d %d %d %d %zu\n", window_size, n_windows, n_values, remmining, prec_sz);

  static const unsigned char nums_b32[33] = "The scalar for this x is unknown";
  secp256k1_fe_t nums_x;
  secp256k1_ge_t nums_ge;
  VERIFY_CHECK(secp256k1_fe_set_b32(&nums_x, nums_b32));
  VERIFY_CHECK(secp256k1_ge_set_xo_var(&nums_ge, &nums_x, 0));
  secp256k1_gej_set_ge(&nums_gej, &nums_ge);
  /* Add G to make the bits in x uniformly distributed. */
  secp256k1_gej_add_ge_var(&nums_gej, &nums_gej, &secp256k1_ge_const_g, NULL);

  secp256k1_gej_t gbase;
  secp256k1_gej_t numsbase;
  gbase = gj; /* (2^w_size)^num_of_windows * G */
  numsbase = nums_gej; /* 2^num_of_windows * nums. */

  for (int j = 0; j < n_windows; j++) {
    //[number of windows][each value from 0 - (2^window_size - 1)]
    table[0] = numsbase;
    for (int i = 1; i < n_values; i++) {
      secp256k1_gej_add_var(&table[i], &table[i-1], &gbase, NULL);
    }

    for (int i = 0; i < window_size; i++) {
      secp256k1_gej_double_var(&gbase, &gbase, NULL);
    }
    /* Multiply numbase by 2. */
    secp256k1_gej_double_var(&numsbase, &numsbase, NULL);
    if (j == n_windows-2) {
      /* In the last iteration, numsbase is (1 - 2^j) * nums instead. */
      secp256k1_gej_neg(&numsbase, &numsbase);
      secp256k1_gej_add_var(&numsbase, &numsbase, &nums_gej, NULL);
    }
    secp256k1_ge_set_all_gej_var(n_values, &prec[j*n_values], &table[0], 0);
  }

  free(table);
  return 0;
}

static void secp256k1_ecmult_gen2(secp256k1_gej_t *r, const unsigned char *seckey){
  unsigned char a[256];
  for (int j = 0; j < 32; j++) {
    for (int i = 0; i < 8; i++) {
      a[i+j*8] = READBIT(seckey[31-j], i);
    }
  }

  r->infinity = 1;
  int bits;

  for (int j = 0; j < n_windows; j++) {
    if (j == n_windows -1 && remmining != 0) {
      bits = 0;
      for (int i = 0; i < remmining; i++) {
        SETBIT(bits,i,a[i + j * WINDOW_SIZE]);
      }
    } else {
      bits = 0;
      for (int i = 0; i < WINDOW_SIZE; i++) {
        SETBIT(bits,i,a[i + j * WINDOW_SIZE]);
      }
    }
#if 1
    secp256k1_gej_add_ge_var(r, r, &prec[j*n_values + bits], NULL);
#else
    secp256k1_gej_add_ge(r, r, &prec[j*n_values + bits]);
#endif
  }
}

#define USE_BL_ARITHMETIC 1
#ifdef USE_BL_ARITHMETIC
static void secp256k1_gej_add_ge_bl(secp256k1_gej_t *r, const secp256k1_gej_t *a, const secp256k1_ge_t *b, secp256k1_fe_t *rzr) {
  secp256k1_fe_t z1z1, /*z1,*/ u2, x1, y1, t0, s2, h, hh, i, j, t1, rr,  v, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11;
  // 7M + 4S + 2 normalize + 22 mul_int/add/negate
  if (a->infinity) {
    VERIFY_CHECK(rzr == NULL);
    secp256k1_gej_set_ge(r, b);
    return;
  }
  if (b->infinity) {
    if (rzr) {
      secp256k1_fe_set_int(rzr, 1);
    }
    *r = *a;
    return;
  }
  r->infinity = 0;

  x1 = a->x; secp256k1_fe_normalize_weak(&x1);
  y1 = a->y; secp256k1_fe_normalize_weak(&y1);

  secp256k1_fe_sqr(&z1z1, &a->z);                                     // z1z1 = z1^2
  secp256k1_fe_mul(&u2, &b->x, &z1z1);                                // u2 = x2*z1z1
  secp256k1_fe_mul(&t0, &a->z, &z1z1);                                // t0 = z1*z1z1
  secp256k1_fe_mul(&s2, &b->y, &t0);                                  // s2 = y2 * t0
  secp256k1_fe_negate(&h, &x1, 1); secp256k1_fe_add(&h, &u2);         // h = u2-x1  (3)
  secp256k1_fe_sqr(&hh,&h);                                           // hh = h^2
  i = hh; secp256k1_fe_mul_int(&i,4);                                 // i = 4*hh
  if (secp256k1_fe_normalizes_to_zero_var(&h)) {
    if (secp256k1_fe_normalizes_to_zero_var(&i)) {
      secp256k1_gej_double_var(r, a, rzr);
    } else {
      if (rzr) {
        secp256k1_fe_set_int(rzr, 0);
      }
      r->infinity = 1;
    }
    return;
  }
  secp256k1_fe_mul(&j,&h,&i);                                         // j = h*i
  secp256k1_fe_negate(&t1, &y1, 1); secp256k1_fe_add(&t1, &s2);       // t1 = s2-y1
  rr = t1; secp256k1_fe_mul_int(&rr, 2);                              // rr = 2 * t1;
  secp256k1_fe_mul(&v, &x1, &i);                                      // v = x1 * i
  secp256k1_fe_sqr(&t2, &rr);                                         // t2 = rr^2
  t3 = v; secp256k1_fe_mul_int(&t3, 2);                               // t3 = 2*v
  secp256k1_fe_negate(&t4, &j, 1);   secp256k1_fe_add(&t4, &t2);      // t4 = t2 - j
  secp256k1_fe_negate(&r->x, &t3, 2); secp256k1_fe_add(&r->x, &t4);   // x3 = t4 - t3;
  //secp256k1_fe_normalize_weak(&r->x);
  secp256k1_fe_negate(&t5, &r->x, 6); secp256k1_fe_add(&t5, &v);      // t5 = v - x3
  secp256k1_fe_mul(&t6,&y1,&j);                                       // t6 = y1 * j
  t7 = t6; secp256k1_fe_mul_int(&t7,2);                               // t7 = 2*t6;
  secp256k1_fe_mul(&t8,&rr,&t5);                                      // t8 = rr* t5;
  secp256k1_fe_negate(&r->y, &t7, 2); secp256k1_fe_add(&r->y,&t8);    // y3 = t8-t7
  //secp256k1_fe_normalize_weak(&r->y);
  t9 = h; secp256k1_fe_add(&t9, &a->z);                               // t9 = z1 + h
  secp256k1_fe_sqr(&t10, &t9);                                        // t10 = t9^2
  secp256k1_fe_negate(&t11, &z1z1, 1); secp256k1_fe_add(&t11, &t10);  // t11 = t10-z1z1
  secp256k1_fe_negate(&r->z, &hh, 1); secp256k1_fe_add(&r->z, &t11);  // z3 = t11 - hh

}

static void secp256k1_ecmult_gen_bl(secp256k1_gej_t *r, const unsigned char *seckey){
  unsigned char a[256];
  for (int j = 0; j < 32; j++){
    for (int i = 0; i < 8; i++){
      a[i+j*8] = READBIT(seckey[31-j], i);
    }
  }

  r->infinity = 1;
  int bits;

  for (int j = 0; j < n_windows; j++) {
    if (j == n_windows -1 && remmining != 0) {
      bits = 0;
      for (int i = 0; i < remmining; i++) {
        SETBIT(bits,i,a[i + j * WINDOW_SIZE]);
      }
      //bits = secp256k1_scalar_get_bits2(a, j * WINDOW_SIZE, remmining);
    } else {
      bits = 0;
      for (int i = 0; i < WINDOW_SIZE; i++) {
        SETBIT(bits,i,a[i + j * WINDOW_SIZE]);
      }
      //bits = secp256k1_scalar_get_bits2(a, j * WINDOW_SIZE, WINDOW_SIZE);
    }
    secp256k1_gej_add_ge_bl(r, r, &prec[j*n_values + bits], NULL);
  }
}
#endif

#ifdef USE_BL_ARITHMETIC
#define secp256k1_ecmult_gen_b32(J, K) secp256k1_ecmult_gen_bl(J, K)
#define secp256k1_gej_add_ge_opt(A, B, C, D) secp256k1_gej_add_ge_bl(A, B, C, D)
#else
#define secp256k1_ecmult_gen_b32(J, K) secp256k1_ecmult_gen2(J, K)
#define secp256k1_gej_add_ge_opt(A, B, C, D) secp256k1_gej_add_ge_var(A, B, C, D)
#endif

int secp256k1_ec_pubkey_create_precomp(unsigned char *pub_chr, int *pub_chr_sz, const unsigned char *seckey) {
  secp256k1_gej_t pj;
  secp256k1_ge_t p;

  secp256k1_ecmult_gen_b32(&pj, seckey);
  secp256k1_ge_set_gej(&p, &pj);

  *pub_chr_sz = 65;
  pub_chr[0] = 4;

  secp256k1_fe_normalize_var(&p.x);
  secp256k1_fe_normalize_var(&p.y);
  secp256k1_fe_get_b32(pub_chr +  1, &p.x);
  secp256k1_fe_get_b32(pub_chr + 33, &p.y);

  return 0;
}

static secp256k1_gej_t *batchpj;
static secp256k1_ge_t  *batchpa;
static secp256k1_fe_t  *batchaz;
static secp256k1_fe_t  *batchai;

int secp256k1_ec_pubkey_batch_init(unsigned int num) {
  if (!batchpj) { batchpj = malloc(sizeof(secp256k1_gej_t)*num); }
  if (!batchpa) { batchpa = malloc(sizeof(secp256k1_ge_t)*num);  }
  if (!batchaz) { batchaz = malloc(sizeof(secp256k1_fe_t)*num);  }
  if (!batchai) { batchai = malloc(sizeof(secp256k1_fe_t)*num);  }
  if (batchpj == NULL || batchpa == NULL || batchaz == NULL || batchai == NULL) {
    return 1;
  } else {
    return 0;
  }
}

void secp256k1_ge_set_all_gej_static(int num, secp256k1_ge_t *batchpa, secp256k1_gej_t *batchpj) {
  size_t i;
  for (i = 0; i < num; i++) {
    batchaz[i] = batchpj[i].z;
  }

  secp256k1_fe_inv_all_var(num, batchai, batchaz);

  for (i = 0; i < num; i++) {
    secp256k1_ge_set_gej_zinv(&batchpa[i], &batchpj[i], &batchai[i]);
  }
}

void secp256k1_b65_set_all_gej_static(int num, unsigned char (*pub)[65], secp256k1_ge_t *batchpa, secp256k1_gej_t *batchpj) {
  int i;
  // convert all jacobian coordinates to affine
  secp256k1_ge_set_all_gej_static(num, batchpa, batchpj);

  // serialize public keys to output
  for (i = 0; i < num; ++i) {
    secp256k1_fe_normalize_var(&batchpa[i].x);
    secp256k1_fe_normalize_var(&batchpa[i].y);

    pub[i][0] = 0x04;
    secp256k1_fe_get_b32(pub[i] +  1, &batchpa[i].x);
    secp256k1_fe_get_b32(pub[i] + 33, &batchpa[i].y);
  }
}

// call secp256k1_ec_pubkey_batch_init first or you get segfaults
int secp256k1_ec_pubkey_batch_incr(unsigned int num, unsigned int skip, unsigned char (*pub)[65], unsigned char (*sec)[32], unsigned char start[32]) {
  // some of the values could be reused between calls, but dealing with the data
  // structures is a pain, and with a reasonable batch size, the perf difference
  // is tiny
  int i;

  unsigned char b32[32];

  secp256k1_scalar_t priv, incr_s;
  secp256k1_gej_t temp;
  secp256k1_ge_t incr_a;

  /* load staring private key */
  secp256k1_scalar_set_b32(&priv, start, NULL);

  /* fill first private */
  secp256k1_scalar_get_b32(sec[0], &priv);

  /* set up increments */
  secp256k1_scalar_set_int(&incr_s, skip);
  secp256k1_scalar_get_b32(b32, &incr_s);

  secp256k1_ecmult_gen_b32(&temp, b32);
  secp256k1_ecmult_gen_b32(&batchpj[0], start);

  /* get affine public point for incrementing */
  secp256k1_ge_set_gej_var(&incr_a, &temp);

  for (i = 1; i < num; ++i) {
    /* increment and write private key */
    secp256k1_scalar_add(&priv, &priv, &incr_s);
    secp256k1_scalar_get_b32(sec[i], &priv);

    /* increment public key */
    secp256k1_gej_add_ge_opt(&batchpj[i], &batchpj[i-1], &incr_a, NULL);
  }

  /* convert all jacobian coordinates to affine */
  secp256k1_ge_set_all_gej_static(num, batchpa, batchpj);
  // serialize public keys
  //secp256k1_b65_set_all_gej_static(num, pub, batchpa, batchpj);

  /* write out formatted public key */
  for (i = 0; i < num; ++i) {
    secp256k1_fe_normalize_var(&batchpa[i].x);
    secp256k1_fe_normalize_var(&batchpa[i].y);

    pub[i][0] = 0x04;
    secp256k1_fe_get_b32(pub[i] +  1, &batchpa[i].x);
    secp256k1_fe_get_b32(pub[i] + 33, &batchpa[i].y);
  }

  return 0;
}

inline static int secp256k1_scalar_double(secp256k1_scalar_t *r, const secp256k1_scalar_t *a) {
  int overflow;
  unsigned int t, u; /* should only ever contain 0 or 1 */

  /* double by bitwise shift left */
#if defined(USE_SCALAR_4X64)
         t = a->d[0] >> 63; r->d[0] =     (a->d[0] << 1);
  u = t; t = a->d[1] >> 63; r->d[1] = u | (a->d[1] << 1);
  u = t; t = a->d[2] >> 63; r->d[2] = u | (a->d[2] << 1);
  u = t; t = a->d[3] >> 63; r->d[3] = u | (a->d[3] << 1);
#elif defined(USE_SCALAR_8X32)
         t = a->d[0] >> 31; r->d[0] =     (a->d[0] << 1);
  u = t; t = a->d[1] >> 31; r->d[1] = u | (a->d[1] << 1);
  u = t; t = a->d[2] >> 31; r->d[2] = u | (a->d[2] << 1);
  u = t; t = a->d[3] >> 31; r->d[3] = u | (a->d[3] << 1);
  u = t; t = a->d[4] >> 31; r->d[4] = u | (a->d[4] << 1);
  u = t; t = a->d[5] >> 31; r->d[5] = u | (a->d[5] << 1);
  u = t; t = a->d[6] >> 31; r->d[6] = u | (a->d[6] << 1);
  u = t; t = a->d[7] >> 31; r->d[7] = u | (a->d[7] << 1);
#else
#error "Please configure libsecp256k1's scalar implementation"
#endif

  overflow = t + secp256k1_scalar_check_overflow(r);
  VERIFY_CHECK(overflow == 0 || overflow == 1);
  secp256k1_scalar_reduce(r, overflow);
  return overflow;
}

// call secp256k1_ec_pubkey_batch_init first or you get segfaults
int secp256k1_ec_pubkey_batch_double(unsigned int num, unsigned char (*pub)[65], unsigned char (*sec)[32], unsigned char start[32]) {
  int i;
  secp256k1_scalar_t priv;

  /* first iteration, load start key and generate public */
  secp256k1_scalar_set_b32(&priv, start, NULL);
  secp256k1_ecmult_gen_b32(&batchpj[0], start);
  secp256k1_scalar_get_b32(sec[0], &priv);

  for (i = 1; i < num; ++i) {
    secp256k1_scalar_double(&priv, &priv);
    /* serialize private key */
    secp256k1_scalar_get_b32(sec[i], &priv);
    /* double public key */
    secp256k1_gej_double_var(&batchpj[i], &batchpj[i-1], NULL);
  }

  /* convert all jacobian coordinates to affine */
  secp256k1_ge_set_all_gej_static(num, batchpa, batchpj);

  /* write out formatted public keys */
  for (i = 0; i < num; ++i) {
    secp256k1_fe_normalize_var(&batchpa[i].x);
    secp256k1_fe_normalize_var(&batchpa[i].y);

    pub[i][0] = 0x04;
    secp256k1_fe_get_b32(pub[i] +  1, &batchpa[i].x);
    secp256k1_fe_get_b32(pub[i] + 33, &batchpa[i].y);
  }

  return 0;
}

// call secp256k1_ec_pubkey_batch_init first or you get segfaults
int secp256k1_ec_pubkey_batch_create(unsigned int num, unsigned char (*pub)[65], unsigned char (*sec)[32]) {
  int i;

  /* generate jacobian coordinates */
  for (i = 0; i < num; ++i) {
    secp256k1_ecmult_gen_b32(&batchpj[i], sec[i]);
  }

  /* convert all jacobian coordinates to affine */
  secp256k1_ge_set_all_gej_static(num, batchpa, batchpj);
  // serialize public keys
  //secp256k1_b65_set_all_gej_static(num, pub, batchpa, batchpj);

  /* write out formatted public key */
  for (i = 0; i < num; ++i) {
    secp256k1_fe_normalize_var(&batchpa[i].x);
    secp256k1_fe_normalize_var(&batchpa[i].y);

    pub[i][0] = 0x04;
    secp256k1_fe_get_b32(pub[i] +  1, &batchpa[i].x);
    secp256k1_fe_get_b32(pub[i] + 33, &batchpa[i].y);
  }

  return 0;
}

int secp256k1_scalar_add_b32(void * out, void * a, void *b) {
  secp256k1_scalar_t tmp_a, tmp_b;

  secp256k1_scalar_set_b32(&tmp_a, a, NULL);
  secp256k1_scalar_set_b32(&tmp_b, b, NULL);
  secp256k1_scalar_add(&tmp_a, &tmp_a, &tmp_b);
  secp256k1_scalar_get_b32(out, &tmp_a);

  return 0;
}

inline static void _priv_add(unsigned char *priv, unsigned char add, int p) {
  priv[p] += add;
  if (priv[p] < add) {
    priv[--p] += 1;
    while (p) {
      if (priv[p] == 0) {
        priv[--p] += 1;
      } else {
        break;
      }
    }
  }
}

void priv_add_uint8(unsigned char *priv, unsigned char add) {
  _priv_add(priv, add, 31);
}

void priv_add_uint32(unsigned char *priv, unsigned int add) {
  int p = 31;
  while (add) {
    _priv_add(priv, add & 255, p--);
    add >>= 8;
  }
}

void priv_double(unsigned char *priv) {
  secp256k1_scalar_t t;
  secp256k1_scalar_set_b32(&t, priv, NULL);
  secp256k1_scalar_double(&t, &t);
  secp256k1_scalar_get_b32(priv, &t);
}

typedef struct {
  secp256k1_gej_t pubj;
  secp256k1_ge_t  inc;
  secp256k1_gej_t incj;
  unsigned int n;
} pubkey_incr_t;

pubkey_incr_t pubkey_incr_ctx;

int secp256k1_ec_pubkey_incr_init(unsigned char *seckey, unsigned int add) {
  unsigned char incr_priv[32];
  memset(incr_priv, 0, sizeof(incr_priv));
  memset(&pubkey_incr_ctx, 0, sizeof(pubkey_incr_ctx));
  priv_add_uint32(incr_priv, add);

  pubkey_incr_ctx.n = add;

  secp256k1_ecmult_gen_b32(&pubkey_incr_ctx.pubj, seckey);
  secp256k1_ecmult_gen_b32(&pubkey_incr_ctx.incj, incr_priv);
  secp256k1_ge_set_gej(&pubkey_incr_ctx.inc, &pubkey_incr_ctx.incj);

  return 0;
}

int secp256k1_ec_pubkey_incr(unsigned char *pub_chr, int *pub_chr_sz, unsigned char *seckey) {
  secp256k1_ge_t p;

  priv_add_uint32(seckey, pubkey_incr_ctx.n);
  secp256k1_gej_add_ge_opt(&pubkey_incr_ctx.pubj, &pubkey_incr_ctx.pubj, &pubkey_incr_ctx.inc, NULL);

  secp256k1_ge_set_gej(&p, &pubkey_incr_ctx.pubj);

  *pub_chr_sz = 65;
  pub_chr[0] = 4;

  secp256k1_fe_normalize_var(&p.x);
  secp256k1_fe_normalize_var(&p.y);
  secp256k1_fe_get_b32(pub_chr +  1, &p.x);
  secp256k1_fe_get_b32(pub_chr + 33, &p.y);

  return 0;
}

void * secp256k1_ec_priv_to_gej(unsigned char *priv) {
  secp256k1_gej_t *gej = malloc(sizeof(secp256k1_gej_t));
  secp256k1_ecmult_gen_b32(gej, priv);

  return gej;
}

int secp256k1_ec_pubkey_add_gej(unsigned char *pub_chr, int *pub_chr_sz, void *add) {
  secp256k1_ge_t  in;
  secp256k1_ge_t  p;

  secp256k1_gej_t out;

  secp256k1_eckey_pubkey_parse(&in, pub_chr, *pub_chr_sz);

  secp256k1_gej_add_ge_opt(&out, (secp256k1_gej_t *)add, &in, NULL);

  secp256k1_ge_set_gej(&p, &out);

  *pub_chr_sz = 65;
  pub_chr[0] = 4;

  secp256k1_fe_normalize_var(&p.x);
  secp256k1_fe_normalize_var(&p.y);
  secp256k1_fe_get_b32(pub_chr +  1, &p.x);
  secp256k1_fe_get_b32(pub_chr + 33, &p.y);

  return 0;
}

/**************************/
/*                        */
/*  BEGIN KEYSTREAM CODE  */
/*                        */
/**************************/

static int ks_tables_built = 0;
static secp256k1_ge_t ks_ge_add[256], ks_ge_sub[256];
static secp256k1_gej_t zeroj;
void ks_tables_init() {
  int i;
  unsigned char key[32];
  secp256k1_gej_t tmp_gej;
  secp256k1_scalar_t tmp_scalar;

  // don't re-run if tables already built
  if (ks_tables_built) return;

  // infinity is the additive identity
  secp256k1_ge_set_infinity(&ks_ge_add[0]);
  secp256k1_ge_set_infinity(&ks_ge_sub[0]);

  for (i = 1; i < 256; ++i) {
    // generate table entry for adding to least signifigant byte
    memset(key, 0, 32);
    key[31] = i;
    secp256k1_ecmult_gen_b32(&tmp_gej, key);
    secp256k1_ge_set_gej_var(&ks_ge_add[i], &tmp_gej);

    // generate table entry for subtracting from most signifigant byte
    memset(key, 0, 32);
    key[0] = i;
    secp256k1_scalar_set_b32(&tmp_scalar, key, NULL);
    secp256k1_scalar_negate(&tmp_scalar, &tmp_scalar);
    secp256k1_scalar_get_b32(key, &tmp_scalar);
    secp256k1_ecmult_gen_b32(&tmp_gej, key);
    secp256k1_ge_set_gej_var(&ks_ge_sub[i], &tmp_gej);
  }

  // not actually valid, but avoids crashing like setting infinity would
  memset(key, 0, 32);
  secp256k1_ecmult_gen_b32(&zeroj, key);

  ks_tables_built = 1;
}

static inline void secp256k1_gej_mul256_var(secp256k1_gej_t *a, secp256k1_gej_t *b, secp256k1_fe_t *rzr) {
  secp256k1_gej_double_var(a, b, rzr); secp256k1_gej_double_var(a, b, rzr);
  secp256k1_gej_double_var(a, b, rzr); secp256k1_gej_double_var(a, b, rzr);
  secp256k1_gej_double_var(a, b, rzr); secp256k1_gej_double_var(a, b, rzr);
  secp256k1_gej_double_var(a, b, rzr); secp256k1_gej_double_var(a, b, rzr);
}

#define KEY_OKAY  0
#define KEY_OVER -1
#define KEY_ZERO -2
// seems to be okay on sandy bridge+ to have misaligned memory
// http://www.agner.org/optimize/blog/read.php?i=142&v=t
static inline int secp256k1_valid_b32(const unsigned char priv[32]) {
  const uint64_t *p64 = (uint64_t *)priv;
  // ffffffff ffffffff ffffffff fffffffe baaedce6 af48a03b bfd25e8c d0364141
  if (        p64[0]  == 0xffffffffffffffffULL &&
      htobe64(p64[1]) >= 0xfffffffffffffffeULL &&
      htobe64(p64[2]) >= 0xbaaedce6af48a03bULL &&
      htobe64(p64[3]) >= 0xbfd25e8cd0364141ULL) {
    // key too big, set public from key bytes
    return KEY_OVER;
  } else if (p64[0] == 0 && p64[1] == 0 && p64[2] == 0 && p64[3] == 0) {
    // key is zero, set public key to infinity
    return KEY_ZERO;
  } else {
    return KEY_OKAY;
  }
}

// call secp256k1_ec_pubkey_batch_init and ks_tables_init first or you get segfaults
int secp256k1_ec_pubkey_batch_stream(unsigned int num, unsigned char (*pub)[65], unsigned char (*sec)[32], unsigned char *stream) {
  int i, overflow;
  int oldbyte_off = 0, newbyte_off = 32;
  unsigned char *key = stream;

  // handle the first key
  memcpy(sec[0], key, 32);
  if ((overflow = secp256k1_valid_b32(key)) == KEY_ZERO) {
    memcpy(&batchpj[0], &zeroj, sizeof(zeroj));
  } else {
    secp256k1_ecmult_gen_b32(&batchpj[0], key);
  }

  ++key;

  for (i = 1; i < num; ++i, ++oldbyte_off, ++newbyte_off, ++key) {
    // copy from keystream into private key output
    memcpy(sec[i], key, 32);
    // if the previous key was an overflow, compute the public point from scratch
    if (overflow != KEY_OKAY) {
      // check for overflow at new position
      if ((overflow = secp256k1_valid_b32(key)) == KEY_ZERO) {
        memcpy(&batchpj[i], &zeroj, sizeof(zeroj));
      } else {
        secp256k1_ecmult_gen_b32(&batchpj[i], key);
      }
    } else {
      if ((overflow = secp256k1_valid_b32(key)) == KEY_ZERO) {
        memcpy(&batchpj[i], &zeroj, sizeof(zeroj));
      } else if (overflow == KEY_OVER) {
        secp256k1_ecmult_gen_b32(&batchpj[i], key);
      } else {
        // subtract the byte being shifted off
        secp256k1_gej_add_ge_var(&batchpj[i], &batchpj[i-1], &ks_ge_sub[stream[oldbyte_off]], NULL);
        // multiply the public point by 256
        secp256k1_gej_mul256_var(&batchpj[i], &batchpj[i], NULL);
        // add in the new byte
        secp256k1_gej_add_ge_var(&batchpj[i], &batchpj[i],   &ks_ge_add[stream[newbyte_off]], NULL);
      }
    }
  }

  // convert all jacobian coordinates to affine
  secp256k1_ge_set_all_gej_static(num, batchpa, batchpj);
  // serialize public keys
  //secp256k1_b65_set_all_gej_static(num, pub, batchpa, batchpj);

  // serialize public keys to output
  for (i = 0; i < num; ++i) {
    secp256k1_fe_normalize_var(&batchpa[i].x);
    secp256k1_fe_normalize_var(&batchpa[i].y);

    pub[i][0] = 0x04;
    secp256k1_fe_get_b32(pub[i] +  1, &batchpa[i].x);
    secp256k1_fe_get_b32(pub[i] + 33, &batchpa[i].y);
  }

  return 0;
}

/**************************/
/*                        */
/*   END KEYSTREAM CODE   */
/*                        */
/**************************/

/*  vim: set ts=2 sw=2 et ai si: */
