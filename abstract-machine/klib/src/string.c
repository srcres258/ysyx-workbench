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

  dst[i] = '\0';

  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i;

  for (i = 0; i < n && src[i]; i++) {
    dst[i] = src[i];
  }

  for (; i < n; i++) {
    dst[i] = '\0';
  }

  return dst;
}

char *strcat(char *dst, const char *src) {
  char *dstp;

  dstp = dst;
  while (*dstp) dstp++;       // 找到末尾
  while ((*dstp++ = *src++)); // 拼接并拷贝结束符

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
  size_t i;

  for (i = 0; i < n; i++) {
    if (s1[i] != s2[i] || s1[i] == '\0' || s2[i] == '\0') {
      return ((unsigned char) s1[i]) - ((unsigned char) s2[i]);
    }
  }

  return 0;
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
  unsigned char *d;
  const unsigned char *s;
  
  d = dst;
  s = src;
  if (d < s) {
    for (size_t i = 0; i < n; i++) {
      d[i] = s[i];
    }
  } else if (d > s) {
    for (size_t i = n; i > 0; i--) {
      d[i - 1] = s[i - 1];
    }
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
