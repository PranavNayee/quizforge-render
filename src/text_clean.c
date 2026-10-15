/*
 * text_clean.c — Text normalization and sentence splitting.
 *
 * Normalizes whitespace, strips control characters, removes bullet symbols
 * and page-number lines, collapses repeated newlines, and splits on sentence
 * boundaries (.!?) while respecting abbreviations and decimal numbers.
 */
#include "text_clean.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Common abbreviations that should not trigger a sentence split ── */
static const char *abbreviations[] = {
    "dr", "mr", "mrs", "ms", "prof", "sr", "jr", "st",
    "fig", "figs", "eq", "eqs", "vol", "vs", "etc",
    "approx", "dept", "univ", "govt", "inc", "corp", "ltd",
    "jan", "feb", "mar", "apr", "jun", "jul", "aug",
    "sep", "oct", "nov", "dec",
    "no", "nos", "op", "al", "ed", "rev", "gen",
    NULL
};

/* Also protect "e.g" and "i.e" (handled specially) */

/* Check if a lowercased word is a known abbreviation. */
static int is_abbreviation(const char *word, size_t len) {
    char buf[32];
    if (len >= sizeof(buf)) return 0;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)tolower((unsigned char)word[i]);
    }
    buf[len] = '\0';
    for (int i = 0; abbreviations[i]; i++) {
        if (strcmp(buf, abbreviations[i]) == 0) return 1;
    }
    return 0;
}

/* Count whitespace-delimited words in a string. */
static int count_words(const char *s) {
    int n = 0;
    int in_word = 0;
    for (; *s; s++) {
        if (isspace((unsigned char)*s)) {
            in_word = 0;
        } else {
            if (!in_word) n++;
            in_word = 1;
        }
    }
    return n;
}

/* Strip leading and trailing whitespace in place, return new length. */
static size_t trim(char *s) {
    size_t len = strlen(s);
    /* Leading */
    size_t start = 0;
    while (start < len && isspace((unsigned char)s[start])) start++;
    /* Trailing */
    size_t end = len;
    while (end > start && isspace((unsigned char)s[end - 1])) end--;
    size_t new_len = end - start;
    if (start > 0) memmove(s, s + start, new_len);
    s[new_len] = '\0';
    return new_len;
}

/* Return 1 if the character before position `pos` is a digit. */
static int digit_before(const char *text, size_t pos) {
    if (pos == 0) return 0;
    return isdigit((unsigned char)text[pos - 1]);
}

/* Return 1 if the character after position `pos` is a digit. */
static int digit_after(const char *text, size_t pos, size_t len) {
    if (pos + 1 >= len) return 0;
    return isdigit((unsigned char)text[pos + 1]);
}

/* Get the word immediately before `pos` (walking backwards). */
static size_t word_before(const char *text, size_t pos, char *buf, size_t bufsize) {
    if (pos == 0) { buf[0] = '\0'; return 0; }
    size_t end = pos;
    size_t start = pos;
    /* Skip backwards over alphabetic characters */
    while (start > 0 && isalpha((unsigned char)text[start - 1])) start--;
    size_t wlen = end - start;
    if (wlen == 0 || wlen >= bufsize) { buf[0] = '\0'; return 0; }
    memcpy(buf, text + start, wlen);
    buf[wlen] = '\0';
    return wlen;
}

/* Check for "e.g." or "i.e." patterns around a period at `pos`. */
static int is_eg_ie(const char *text, size_t pos) {
    /* Pattern: X.Y. where X and Y are single letters */
    if (pos < 2) return 0;
    if (text[pos - 2] == '.' && isalpha((unsigned char)text[pos - 1])) {
        /* Looks like ?. followed by letter. — e.g. "e.g." or "i.e." */
        return 1;
    }
    /* Also check: the word before might be a single letter (start of e.g) */
    if (pos >= 1 && isalpha((unsigned char)text[pos - 1])) {
        /* Single letter before dot: could be start of multi-dot abbreviation */
        if (pos >= 2 && !isalpha((unsigned char)text[pos - 2]) &&
            !isdigit((unsigned char)text[pos - 2]) && text[pos - 2] != '.') {
            /* Single letter word before dot — might be abbreviation initial */
        }
    }
    return 0;
}

/* Check if a line looks like a page number (e.g., "Page 12", "- 5 -"). */
static int is_page_number_line(const char *line) {
    /* Skip whitespace */
    while (*line && isspace((unsigned char)*line)) line++;
    if (*line == '\0') return 1; /* Empty line */

    /* "Page N" or "page N" */
    if (strncasecmp(line, "page", 4) == 0 && isspace((unsigned char)line[4])) {
        return 1;
    }

    /* Pure number line */
    const char *p = line;
    if (*p == '-') p++;
    while (*p && isspace((unsigned char)*p)) p++;
    int has_digit = 0;
    while (*p && isdigit((unsigned char)*p)) { has_digit = 1; p++; }
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '-') p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0' && has_digit) return 1;

    return 0;
}

/* ── Main cleaning and splitting function ──────────────────────────── */

CleanResult text_clean_and_split(const char *input, size_t input_len) {
    CleanResult result;
    memset(&result, 0, sizeof(result));

    if (!input || input_len == 0) return result;

    /* Cap input at 100KB for safety */
    if (input_len > 102400) input_len = 102400;

    /* Phase 1: Normalize the raw text into a clean buffer */
    StrBuf cleaned;
    strbuf_init(&cleaned, input_len + 1);

    for (size_t i = 0; i < input_len; i++) {
        unsigned char c = (unsigned char)input[i];

        /* Strip non-printable control chars except newline, tab, space */
        if (c < 0x20 && c != '\n' && c != '\t') continue;
        if (c == 0x7F) continue;

        /* Strip common bullet/list symbols: •, ◦, ▪, ►, ★ */
        /* These are multi-byte UTF-8 — check for common patterns */
        if (c == 0xE2 && i + 2 < input_len) {
            unsigned char b1 = (unsigned char)input[i + 1];
            unsigned char b2 = (unsigned char)input[i + 2];
            /* • U+2022: E2 80 A2 */
            if (b1 == 0x80 && b2 == 0xA2) { i += 2; continue; }
            /* ◦ U+25E6: E2 97 A6 */
            if (b1 == 0x97 && b2 == 0xA6) { i += 2; continue; }
            /* ▪ U+25AA: E2 96 AA */
            if (b1 == 0x96 && b2 == 0xAA) { i += 2; continue; }
            /* ► U+25BA: E2 96 BA */
            if (b1 == 0x96 && b2 == 0xBA) { i += 2; continue; }
            /* ★ U+2605: E2 98 85 */
            if (b1 == 0x98 && b2 == 0x85) { i += 2; continue; }
            /* – U+2013 and — U+2014: treat as space */
            if (b1 == 0x80 && (b2 == 0x93 || b2 == 0x94)) {
                strbuf_append(&cleaned, " ", 1);
                i += 2;
                continue;
            }
        }

        /* Replace tabs with spaces */
        if (c == '\t') {
            strbuf_append(&cleaned, " ", 1);
            continue;
        }

        /* ASCII bullets: *, -, at start of a line → remove */
        if ((c == '*' || c == '-') && cleaned.len > 0 &&
            cleaned.data[cleaned.len - 1] == '\n') {
            /* Skip the bullet character */
            continue;
        }

        strbuf_append(&cleaned, (char *)&c, 1);
    }

    /* Phase 2: Collapse multiple whitespace and newlines */
    StrBuf normalized;
    strbuf_init(&normalized, cleaned.len + 1);

    int prev_newline = 0;
    int prev_space = 0;

    for (size_t i = 0; i < cleaned.len; i++) {
        char c = cleaned.data[i];
        if (c == '\n') {
            if (!prev_newline) {
                strbuf_append(&normalized, "\n", 1);
            }
            prev_newline = 1;
            prev_space = 0;
        } else if (c == ' ') {
            if (!prev_space && !prev_newline) {
                strbuf_append(&normalized, " ", 1);
            }
            prev_space = 1;
        } else {
            prev_newline = 0;
            prev_space = 0;
            strbuf_append(&normalized, &c, 1);
        }
    }

    strbuf_free(&cleaned);

    /* Save the cleaned text for display */
    result.cleaned_text = safe_strdup(normalized.data);

    /* Phase 3: Split into lines first, filter page-number lines */
    /* Then split each line into sentences */

    /* Collect sentences into a dynamic array */
    int cap = 256;
    result.sentences = (Sentence *)malloc(sizeof(Sentence) * (size_t)cap);
    if (!result.sentences) {
        strbuf_free(&normalized);
        return result;
    }
    result.count = 0;
    result.total_words = 0;

    const char *text = normalized.data;
    size_t text_len = normalized.len;

    /* Walk through the text and find sentence boundaries */
    size_t sent_start = 0;
    int sentence_index = 0;

    for (size_t i = 0; i <= text_len; i++) {
        int is_boundary = 0;
        char c = (i < text_len) ? text[i] : '\0';

        if (i == text_len) {
            /* End of text — flush the remaining sentence */
            is_boundary = 1;
        } else if (c == '\n') {
            is_boundary = 1;
        } else if (c == '!' || c == '?') {
            is_boundary = 1;
        } else if (c == '.') {
            /* Check if this period is a sentence boundary */

            /* Decimal number: digit.digit → not a boundary */
            if (digit_before(text, i) && digit_after(text, i, text_len)) {
                continue;
            }

            /* e.g. / i.e. pattern */
            if (is_eg_ie(text, i)) {
                continue;
            }

            /* Known abbreviation before the dot */
            char word_buf[32];
            size_t wlen = word_before(text, i, word_buf, sizeof(word_buf));
            if (wlen > 0 && wlen <= 4 && is_abbreviation(word_buf, wlen)) {
                continue;
            }

            /* Single letter before dot (like middle initials) — not a boundary */
            if (wlen == 1) {
                continue;
            }

            is_boundary = 1;
        }

        if (is_boundary) {
            /* Extract the sentence from sent_start to i (inclusive of the punctuation) */
            size_t end = (i < text_len && c != '\n') ? i + 1 : i;
            size_t slen = end - sent_start;
            if (slen > 0) {
                char *sent = (char *)malloc(slen + 1);
                if (sent) {
                    memcpy(sent, text + sent_start, slen);
                    sent[slen] = '\0';
                    size_t tlen = trim(sent);

                    /* Skip page-number lines */
                    if (tlen > 0 && !is_page_number_line(sent)) {
                        int wc = count_words(sent);

                        /* Only keep sentences with 5-60 words */
                        if (wc >= 5 && wc <= 60) {
                            /* Grow array if needed */
                            if (result.count >= cap) {
                                cap *= 2;
                                Sentence *tmp = (Sentence *)realloc(
                                    result.sentences,
                                    sizeof(Sentence) * (size_t)cap);
                                if (!tmp) { free(sent); continue; }
                                result.sentences = tmp;
                            }
                            result.sentences[result.count].text = sent;
                            result.sentences[result.count].original_index = sentence_index;
                            result.sentences[result.count].word_count = wc;
                            result.total_words += wc;
                            result.count++;
                            sent = NULL; /* Ownership transferred */
                        }
                        sentence_index++;
                    }
                    free(sent); /* Free if not transferred */
                }
            }
            sent_start = (i < text_len) ? i + 1 : text_len;
        }
    }

    strbuf_free(&normalized);
    return result;
}

/* Free all memory held by a CleanResult. */
void clean_result_free(CleanResult *cr) {
    if (cr->sentences) {
        for (int i = 0; i < cr->count; i++) {
            free(cr->sentences[i].text);
        }
        free(cr->sentences);
        cr->sentences = NULL;
    }
    free(cr->cleaned_text);
    cr->cleaned_text = NULL;
    cr->count = 0;
}
