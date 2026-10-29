#ifndef PARSE_H
#define PARSE_H
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
/* Strict decimal parsing; reject signs, whitespace, overflow and trailing text. */
static int parse_u64(const char *s, unsigned long long max, unsigned long long *v) {
    if (!s || !*s) return 0;
    for (const char *p=s; *p; ++p) if (*p<'0' || *p>'9') return 0;
    errno=0; char *end; unsigned long long n=strtoull(s,&end,10);
    if (errno || *end || n>max) return 0;
    *v=n; return 1;
}
#endif
