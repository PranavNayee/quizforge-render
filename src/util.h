/*
 * util.h — Dynamic string buffer, hash map, and miscellaneous helpers.
 */
#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

/* ── Dynamic string buffer ─────────────────────────────────────────── */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} StrBuf;

/* Initialize a string buffer with the given initial capacity. */
void   strbuf_init(StrBuf *sb, size_t init_cap);

/* Append len bytes of raw data. */
void   strbuf_append(StrBuf *sb, const char *s, size_t len);

/* Append a null-terminated string. */
void   strbuf_appends(StrBuf *sb, const char *s);

/* Append a formatted string (printf style). */
void   strbuf_appendf(StrBuf *sb, const char *fmt, ...)
       __attribute__((format(printf, 2, 3)));

/* Reset length to 0 (keeps the allocation). */
void   strbuf_clear(StrBuf *sb);

/* Free all memory. */
void   strbuf_free(StrBuf *sb);

/* ── Simple hash map (string → double) ─────────────────────────────── */

typedef struct HMEntry {
    char           *key;
    double          value;
    int             int_val;      /* auxiliary integer field */
    struct HMEntry *next;
} HMEntry;

typedef struct {
    HMEntry **buckets;
    size_t    n_buckets;
    size_t    count;
} HashMap;

/* Create a hash map with the given bucket count. */
void    hm_init(HashMap *hm, size_t n_buckets);

/* Set key to value (inserts or updates). */
void    hm_set(HashMap *hm, const char *key, double value);

/* Get value for key; returns NULL if not found. */
HMEntry *hm_get(HashMap *hm, const char *key);

/* Increment the value for key by delta (inserts with delta if missing). */
void    hm_inc(HashMap *hm, const char *key, double delta);

/* Increment the integer field for key by delta. */
void    hm_inc_int(HashMap *hm, const char *key, int delta);

/* Free all entries and the bucket array. */
void    hm_free(HashMap *hm);

/* Iterate: call fn(key, value, int_val, userdata) for each entry. */
typedef void (*hm_iter_fn)(const char *key, double value, int int_val,
                           void *userdata);
void    hm_foreach(HashMap *hm, hm_iter_fn fn, void *userdata);

/* Return the number of entries. */
size_t  hm_count(HashMap *hm);

/* ── String helpers ──────────────────────────────────────────────────── */

/* Duplicate a string (safe: returns "" on NULL). */
char   *safe_strdup(const char *s);

/* Bounded string copy that always null-terminates. */
void    safe_strncpy(char *dst, const char *src, size_t n);

/* Get current time in milliseconds (monotonic). */
uint64_t time_ms(void);

/* Return the minimum of two size_t values. */
size_t  size_min(size_t a, size_t b);

/* Return the maximum of two size_t values. */
size_t  size_max(size_t a, size_t b);

/* Escape a string for safe JSON embedding (caller must free). */
char   *json_escape(const char *s);

#endif /* UTIL_H */
