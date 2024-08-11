#ifndef DS_H
#define DS_H

#ifndef CUSTOM_ASSERT
#include <assert.h>
#define CUSTOM_ASSERT(c) assert(c)
#endif

#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;

#define List(T) struct { T *buf; size_t cap, count; }

typedef struct {
  u8 *buf;
  size_t cap;
  ptrdiff_t pos;
} Arena;

Arena arena_init(size_t cap);
void *arena_alloc(Arena *al, size_t len);
void arena_free_all(Arena *al);

#ifdef DS_IMPL
#undef DS_IMPL

#define arrlen(...) (size_t)(sizeof(__VA_ARGS__) / sizeof(*__VA_ARGS__))

#define da_push(xs, x) \
  do { \
    if ((xs)->count >= (xs)->cap) { \
      if ((xs)->cap == 0) (xs)->cap = 256; \
      else (xs)->cap *= 2; \
      (xs)->buf = realloc((xs)->buf, (xs)->cap*sizeof(*(xs)->buf)); \
    } \
 \
    (xs)->buf[(xs)->count++] = (x); \
  } while (0)

#define da_pop(xs) \
  do { \
    assert ((xs)->count > 0); \
    (xs)->count -= 1; \
  } while (0)

#define da_last(xs) (assert((xs)->count > 0), (xs)->buf[(xs)->count - 1])

#define da_init(xs) \
do { \
  assert((xs)->cap > 0); \
  (xs)->buf = malloc((xs)->cap * sizeof((xs)->buf[0])); \
} while (0);

Arena arena_init(size_t cap) {
  CUSTOM_ASSERT(cap > 0);

  Arena al = { .cap = cap, };
  al.buf = malloc(al.cap);

  return al;
}

void *arena_alloc(Arena *al, size_t len) {
  CUSTOM_ASSERT(al->pos + len <= al->cap && "Not enough memory in arena");

  void *mem = &al->buf[al->pos];
  al->pos += len;

  return mem;
}

void arena_free_all(Arena *al) {
  al->pos = 0;
}

#endif

#endif
