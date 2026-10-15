/*
 * util.c — Dynamic string buffer, hash map, and miscellaneous helpers.
 */
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>

/* ── Dynamic string buffer ─────────────────────────────────────────── */

/* Initialize a string buffer with the given initial capacity. */
void strbuf_init(StrBuf *sb, size_t init_cap) {
    if (init_cap < 64) init_cap = 64;
    sb->data = (char *)malloc(init_cap);
    if (!sb->data) {
        fprintf(stderr, "strbuf_init: malloc failed\n");
        sb->data = (char *)malloc(64);
        if (!sb->data) abort();
        init_cap = 64;
    }
    sb->data[0] = '\0';
    sb->len = 0;
    sb->cap = init_cap;
}

/* Ensure there is room for at least `need` more bytes. */
static void strbuf_grow(StrBuf *sb, size_t need) {
    if (sb->len + need + 1 <= sb->cap) return;
    size_t new_cap = sb->cap * 2;
    while (new_cap < sb->len + need + 1) new_cap *= 2;
    char *p = (char *)realloc(sb->data, new_cap);
    if (!p) {
        fprintf(stderr, "strbuf_grow: realloc failed\n");
        return;
    }
    sb->data = p;
    sb->cap  = new_cap;
}

/* Append len bytes of raw data. */
void strbuf_append(StrBuf *sb, const char *s, size_t len) {
    if (!s || len == 0) return;
    strbuf_grow(sb, len);
    memcpy(sb->data + sb->len, s, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

/* Append a null-terminated string. */
void strbuf_appends(StrBuf *sb, const char *s) {
    if (!s) return;
    strbuf_append(sb, s, strlen(s));
}

/* Append a formatted string (printf style). */
void strbuf_appendf(StrBuf *sb, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);

    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }

    strbuf_grow(sb, (size_t)n);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap2);
    sb->len += (size_t)n;
    va_end(ap2);
}

/* Reset length to 0 (keeps the allocation). */
void strbuf_clear(StrBuf *sb) {
    sb->len = 0;
    if (sb->data) sb->data[0] = '\0';
}

/* Free all memory. */
void strbuf_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

/* ── Hash map ──────────────────────────────────────────────────────── */

/* FNV-1a hash for strings. */
static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    return h;
}

/* Create a hash map with the given bucket count. */
void hm_init(HashMap *hm, size_t n_buckets) {
    if (n_buckets < 16) n_buckets = 16;
    hm->buckets = (HMEntry **)calloc(n_buckets, sizeof(HMEntry *));
    if (!hm->buckets) {
        fprintf(stderr, "hm_init: calloc failed\n");
        hm->n_buckets = 0;
        hm->count = 0;
        return;
    }
    hm->n_buckets = n_buckets;
    hm->count = 0;
}

/* Set key to value (inserts or updates). */
void hm_set(HashMap *hm, const char *key, double value) {
    if (!hm->buckets || !key) return;
    uint32_t idx = fnv1a(key) % (uint32_t)hm->n_buckets;
    HMEntry *e = hm->buckets[idx];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            e->value = value;
            return;
        }
        e = e->next;
    }
    e = (HMEntry *)malloc(sizeof(HMEntry));
    if (!e) return;
    e->key     = safe_strdup(key);
    e->value   = value;
    e->int_val = 0;
    e->next    = hm->buckets[idx];
    hm->buckets[idx] = e;
    hm->count++;
}

/* Get entry for key; returns NULL if not found. */
HMEntry *hm_get(HashMap *hm, const char *key) {
    if (!hm->buckets || !key) return NULL;
    uint32_t idx = fnv1a(key) % (uint32_t)hm->n_buckets;
    HMEntry *e = hm->buckets[idx];
    while (e) {
        if (strcmp(e->key, key) == 0) return e;
        e = e->next;
    }
    return NULL;
}

/* Increment the value for key by delta (inserts with delta if missing). */
void hm_inc(HashMap *hm, const char *key, double delta) {
    HMEntry *e = hm_get(hm, key);
    if (e) {
        e->value += delta;
    } else {
        hm_set(hm, key, delta);
    }
}

/* Increment the integer field for key by delta. */
void hm_inc_int(HashMap *hm, const char *key, int delta) {
    HMEntry *e = hm_get(hm, key);
    if (e) {
        e->int_val += delta;
    } else {
        hm_set(hm, key, 0.0);
        e = hm_get(hm, key);
        if (e) e->int_val = delta;
    }
}

/* Free all entries and the bucket array. */
void hm_free(HashMap *hm) {
    if (!hm->buckets) return;
    for (size_t i = 0; i < hm->n_buckets; i++) {
        HMEntry *e = hm->buckets[i];
        while (e) {
            HMEntry *next = e->next;
            free(e->key);
            free(e);
            e = next;
        }
    }
    free(hm->buckets);
    hm->buckets   = NULL;
    hm->n_buckets  = 0;
    hm->count      = 0;
}

/* Iterate: call fn(key, value, int_val, userdata) for each entry. */
void hm_foreach(HashMap *hm, hm_iter_fn fn, void *userdata) {
    if (!hm->buckets) return;
    for (size_t i = 0; i < hm->n_buckets; i++) {
        HMEntry *e = hm->buckets[i];
        while (e) {
            fn(e->key, e->value, e->int_val, userdata);
            e = e->next;
        }
    }
}

/* Return the number of entries. */
size_t hm_count(HashMap *hm) {
    return hm->count;
}

/* ── String helpers ──────────────────────────────────────────────────── */

/* Duplicate a string (safe: returns "" on NULL). */
char *safe_strdup(const char *s) {
    if (!s) s = "";
    size_t len = strlen(s);
    char *d = (char *)malloc(len + 1);
    if (!d) {
        d = (char *)malloc(1);
        if (d) d[0] = '\0';
        return d;
    }
    memcpy(d, s, len + 1);
    return d;
}

/* Bounded string copy that always null-terminates. */
void safe_strncpy(char *dst, const char *src, size_t n) {
    if (n == 0) return;
    size_t i = 0;
    for (; i < n - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* Get current time in milliseconds (monotonic). */
uint64_t time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Return the minimum of two size_t values. */
size_t size_min(size_t a, size_t b) {
    return a < b ? a : b;
}

/* Return the maximum of two size_t values. */
size_t size_max(size_t a, size_t b) {
    return a > b ? a : b;
}

/* Escape a string for safe JSON embedding (caller must free). */
char *json_escape(const char *s) {
    if (!s) return safe_strdup("");
    StrBuf sb;
    strbuf_init(&sb, strlen(s) + 32);
    for (; *s; s++) {
        switch (*s) {
            case '"':  strbuf_appends(&sb, "\\\""); break;
            case '\\': strbuf_appends(&sb, "\\\\"); break;
            case '\b': strbuf_appends(&sb, "\\b");  break;
            case '\f': strbuf_appends(&sb, "\\f");  break;
            case '\n': strbuf_appends(&sb, "\\n");  break;
            case '\r': strbuf_appends(&sb, "\\r");  break;
            case '\t': strbuf_appends(&sb, "\\t");  break;
            default:
                if ((unsigned char)*s < 0x20) {
                    /* Escape control characters as \u00XX */
                    strbuf_appendf(&sb, "\\u%04x", (unsigned char)*s);
                } else {
                    strbuf_append(&sb, s, 1);
                }
                break;
        }
    }
    char *result = sb.data;    /* Transfer ownership */
    sb.data = NULL;
    return result;
}
