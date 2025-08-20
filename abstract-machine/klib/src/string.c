#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

size_t strlen(const char *s) {
  size_t c;
  const char *p;

  c = 0;
  for (p = s; *p; p++) {
    c++;
  }

  return c;
}

char *strcpy(char *dst, const char *src) {
  size_t i;

  for (i = 0; src[i]; i++) {
    dst[i] = src[i];
  }

  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i;

  for (i = 0; i < n && src[i]; i++) {
    dst[i] = src[i];
  }

  return dst;
}

char *strcat(char *dst, const char *src) {
  size_t dstlen, srclen, c;
  char *dstp;
  const char *srcp;

  for (
    dstlen = strlen(dst), srclen = strlen(src),
    dstp = dst + dstlen, srcp = src,
    c = 0;

    c < srclen;

    dstp++, srcp++, c++
  ) {
    *dstp = *srcp;
  }
  *dstp = '\0';

  return dst;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }

  return ((int) *((const unsigned char *) s1)) -
    ((int) *((const unsigned char *) s2));
}

int strncmp(const char *s1, const char *s2, size_t n) {
  const char *s1p, *s2p;
  size_t c;

  for (
    s1p = s1, s2p = s2, c = 0;
    c < n && *s1p && (*s1p == *s2p);
    s1p++, s2p++, c++
  );

  return ((int) *((const unsigned char *) s1p)) -
    ((int) *((const unsigned char *) s2p));
}

void *memset(void *s, int c, size_t n) {
  unsigned char *dest = s;
  size_t i;

  for (i = 0; i < n; i++) {
    dest[i] = (unsigned char) c;
  }

  return s;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *dstp;
  const unsigned char *srcp;
  size_t c;

  for (
    dstp = (unsigned char *) dst,
    srcp = (const unsigned char *) src,
    c = 0;

    c < n;

    dstp++, srcp++, c++
  ) {
    *dstp = *srcp;
  }

  return dst;
}

void *memcpy(void *out, const void *in, size_t n) {
  // 转为对 memmove 的调用（该函数保证当内存发生局部重叠时也能正确拷贝）。
  return memmove(out, in, n);
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *s1p, *s2p;
  int res;

  for (
    s1p = (const unsigned char *) s1,
    s2p = (const unsigned char *) s2,
    res = 0;
    
    n > 0;
    
    s1p++, s2p++, n--
  ) {
    if ((res = *s1p - *s2p) != 0) {
      break;
    }
  }

  return res;
}

#endif
