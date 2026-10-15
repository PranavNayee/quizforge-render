/*
 * tfidf.c — Tokenizer, stopword filter, suffix stemmer, and TF-IDF scoring.
 *
 * Scoring formula for each sentence S:
 *   raw_score = Σ (tf(t) * idf(t)) / sqrt(|tokens(S)|)
 * where:
 *   tf(t)  = count of term t across the entire document
 *   idf(t) = log((N + 1) / (df(t) + 1)) + 1
 *   N      = total number of sentences
 *   df(t)  = number of sentences containing term t
 *
 * Bonus/penalty adjustments applied as multipliers on the raw score.
 */
#include "tfidf.h"
#include "util.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── 170 English stopwords ─────────────────────────────────────────── */

static const char *stopwords[] = {
    "a","about","above","after","again","against","all","am","an","and",
    "any","are","aren","as","at","be","because","been","before","being",
    "below","between","both","but","by","can","could","did","didn","do",
    "does","doesn","doing","don","down","during","each","few","for","from",
    "further","get","got","had","hadn","has","hasn","have","haven","having",
    "he","her","here","hers","herself","him","himself","his","how","i",
    "if","in","into","is","isn","it","its","itself","just","let",
    "ll","may","me","might","more","most","mustn","my","myself","need",
    "no","nor","not","now","of","off","on","once","only","or",
    "other","our","ours","ourselves","out","over","own","re","s","same",
    "shall","shan","she","should","shouldn","so","some","such","t","than",
    "that","the","their","theirs","them","themselves","then","there","these","they",
    "this","those","through","to","too","under","until","up","us","ve",
    "very","was","wasn","we","were","weren","what","when","where","which",
    "while","who","whom","why","will","with","won","would","wouldn","you",
    "your","yours","yourself","yourselves","also","already","always","another",
    "around","away","back","even","ever","every","go","goes","going","gone",
    "got","had","has","have","here","how","however","keep","many","much",
    NULL
};

/* Hash set for O(1) stopword lookup. */
static HashMap sw_map;
static int sw_init_done = 0;

/* Initialize the stopword hash set (called once). */
static void init_stopwords(void) {
    if (sw_init_done) return;
    hm_init(&sw_map, 512);
    for (int i = 0; stopwords[i]; i++) {
        hm_set(&sw_map, stopwords[i], 1.0);
    }
    sw_init_done = 1;
}

/* Check if a word is a stopword. */
int is_stopword(const char *word) {
    init_stopwords();
    return hm_get(&sw_map, word) != NULL;
}

/* ── Simple suffix stemmer ─────────────────────────────────────────── */

/* Apply simple suffix stemmer to a word (in place).
 * Removes common English suffixes: -ing, -tion, -sion, -ment, -ness,
 * -ly, -ed, -er, -est, -ies→y, -ous, -ive, -able, -ible, -al, -s.
 * Only removes suffix if the remaining stem is >= 3 characters.
 */
void stem_word(char *word) {
    size_t len = strlen(word);
    if (len < 4) return; /* Too short to stem */

    /* -tion, -sion → remove */
    if (len >= 5 && (strcmp(word + len - 4, "tion") == 0 ||
                     strcmp(word + len - 4, "sion") == 0)) {
        if (len - 4 >= 3) { word[len - 4] = '\0'; return; }
    }

    /* -ment */
    if (len >= 5 && strcmp(word + len - 4, "ment") == 0) {
        if (len - 4 >= 3) { word[len - 4] = '\0'; return; }
    }

    /* -ness */
    if (len >= 5 && strcmp(word + len - 4, "ness") == 0) {
        if (len - 4 >= 3) { word[len - 4] = '\0'; return; }
    }

    /* -able, -ible */
    if (len >= 5 && (strcmp(word + len - 4, "able") == 0 ||
                     strcmp(word + len - 4, "ible") == 0)) {
        if (len - 4 >= 3) { word[len - 4] = '\0'; return; }
    }

    /* -ing */
    if (len >= 4 && strcmp(word + len - 3, "ing") == 0) {
        if (len - 3 >= 3) { word[len - 3] = '\0'; return; }
    }

    /* -ous */
    if (len >= 4 && strcmp(word + len - 3, "ous") == 0) {
        if (len - 3 >= 3) { word[len - 3] = '\0'; return; }
    }

    /* -ive */
    if (len >= 4 && strcmp(word + len - 3, "ive") == 0) {
        if (len - 3 >= 3) { word[len - 3] = '\0'; return; }
    }

    /* -ies → y */
    if (len >= 4 && strcmp(word + len - 3, "ies") == 0) {
        if (len - 3 >= 3) { word[len - 3] = '\0'; }
        else { word[len - 2] = 'y'; word[len - 1] = '\0'; }
        return;
    }

    /* -ly */
    if (len >= 4 && strcmp(word + len - 2, "ly") == 0) {
        if (len - 2 >= 3) { word[len - 2] = '\0'; return; }
    }

    /* -ed (but not -eed) */
    if (len >= 4 && strcmp(word + len - 2, "ed") == 0 && word[len - 3] != 'e') {
        if (len - 2 >= 3) { word[len - 2] = '\0'; return; }
    }

    /* -er (but not -eer) */
    if (len >= 4 && strcmp(word + len - 2, "er") == 0 && word[len - 3] != 'e') {
        if (len - 2 >= 3) { word[len - 2] = '\0'; return; }
    }

    /* -est */
    if (len >= 4 && strcmp(word + len - 3, "est") == 0) {
        if (len - 3 >= 3) { word[len - 3] = '\0'; return; }
    }

    /* -al */
    if (len >= 4 && strcmp(word + len - 2, "al") == 0) {
        if (len - 2 >= 3) { word[len - 2] = '\0'; return; }
    }

    /* -s (plural), but not -ss */
    if (len >= 4 && word[len - 1] == 's' && word[len - 2] != 's') {
        if (len - 1 >= 3) { word[len - 1] = '\0'; return; }
    }
}

/* ── Tokenizer ─────────────────────────────────────────────────────── */

/* Tokenize a sentence: lowercase, keep alphabetic words (and digits),
 * remove stopwords, apply stemmer.
 * Returns an array of stemmed tokens; sets *out_count.
 * Caller must free each token and the array.
 */
char **tokenize(const char *text, int *out_count) {
    init_stopwords();
    *out_count = 0;

    if (!text || !*text) return NULL;

    int cap = 32;
    char **tokens = (char **)malloc(sizeof(char *) * (size_t)cap);
    if (!tokens) return NULL;

    const char *p = text;
    char word[128];
    int wlen = 0;

    while (1) {
        unsigned char c = (unsigned char)*p;
        int is_word_char = (isalpha(c) || (isdigit(c) && wlen > 0));

        if (is_word_char && wlen < 126) {
            word[wlen++] = (char)tolower(c);
        } else {
            if (wlen >= 2) {
                word[wlen] = '\0';

                /* Skip stopwords */
                if (!is_stopword(word)) {
                    /* Apply stemmer */
                    stem_word(word);

                    /* Skip very short stems */
                    if (strlen(word) >= 2) {
                        if (*out_count >= cap) {
                            cap *= 2;
                            char **tmp = (char **)realloc(
                                tokens, sizeof(char *) * (size_t)cap);
                            if (!tmp) break;
                            tokens = tmp;
                        }
                        tokens[*out_count] = safe_strdup(word);
                        (*out_count)++;
                    }
                }
            }
            wlen = 0;
        }

        if (*p == '\0') break;
        p++;
    }

    if (*out_count == 0) {
        free(tokens);
        return NULL;
    }
    return tokens;
}

/* Find the original lowercase word whose stem equals `part` (or the word itself). */
static int find_surface_word(const char *part, const Sentence *sentences,
                             int count, char *out, size_t out_sz) {
    char word[128];
    for (int i = 0; i < count; i++) {
        const char *p = sentences[i].text;
        int wlen = 0;
        while (1) {
            unsigned char ch = (unsigned char)*p;
            if ((isalpha(ch) || (isdigit(ch) && wlen > 0)) && wlen < 126) {
                word[wlen++] = (char)tolower(ch);
            } else {
                if (wlen >= 2) {
                    word[wlen] = '\0';
                    int match = (strcmp(word, part) == 0);
                    if (!match && !is_stopword(word)) {
                        char tmp[128];
                        memcpy(tmp, word, (size_t)wlen + 1);
                        stem_word(tmp);
                        match = (strcmp(tmp, part) == 0);
                    }
                    if (match) {
                        snprintf(out, out_sz, "%s", word);
                        return 1;
                    }
                }
                wlen = 0;
            }
            if (*p == '\0') break;
            p++;
        }
    }
    return 0;
}

/* Convert a stemmed term/phrase back to readable original words. */
char *stem_to_surface(const char *stem_term, const Sentence *sentences, int count) {
    init_stopwords();
    if (!stem_term) return safe_strdup("");
    size_t cap = strlen(stem_term) * 2 + 64;
    char *res = (char *)malloc(cap);
    if (!res) return safe_strdup(stem_term);
    res[0] = '\0';
    size_t used = 0;

    const char *p = stem_term;
    while (*p) {
        char part[128];
        size_t n = 0;
        while (*p && *p != ' ' && n < sizeof(part) - 1) part[n++] = *p++;
        part[n] = '\0';
        while (*p == ' ') p++;
        if (n == 0) continue;

        char surf[128];
        if (!find_surface_word(part, sentences, count, surf, sizeof(surf))) {
            snprintf(surf, sizeof(surf), "%s", part);
        }
        size_t sl = strlen(surf);
        if (used + sl + 2 > cap) {
            cap = (used + sl + 2) * 2;
            char *t = (char *)realloc(res, cap);
            if (!t) return res;
            res = t;
        }
        if (used > 0) res[used++] = ' ';
        memcpy(res + used, surf, sl);
        used += sl;
        res[used] = '\0';
    }
    return res;
}

/* ── Definition pattern detection ──────────────────────────────────── */

/* Check if a sentence contains a definition pattern for a score boost. */
static int has_definition_pattern(const char *text) {
    const char *patterns[] = {
        "is defined as", "refers to", "is called", " means ",
        " is a ", " is an ", " is the ", "known as", "defined by",
        "describes ", "consists of", "composed of",
        NULL
    };
    /* Case-insensitive search */
    size_t tlen = strlen(text);
    char *lower = (char *)malloc(tlen + 1);
    if (!lower) return 0;
    for (size_t i = 0; i < tlen; i++) {
        lower[i] = (char)tolower((unsigned char)text[i]);
    }
    lower[tlen] = '\0';

    int found = 0;
    for (int i = 0; patterns[i]; i++) {
        if (strstr(lower, patterns[i])) {
            found = 1;
            break;
        }
    }
    free(lower);
    return found;
}

/* Check if a sentence contains numbers/dates/formulas. */
static int has_numbers(const char *text) {
    for (const char *p = text; *p; p++) {
        if (isdigit((unsigned char)*p)) return 1;
    }
    return 0;
}

/* Check if a sentence contains a capitalized multi-word term. */
static int has_capitalized_phrase(const char *text) {
    /* Look for two+ consecutive capitalized words (not at sentence start) */
    const char *p = text;
    /* Skip first word (always capitalized) */
    while (*p && !isspace((unsigned char)*p)) p++;
    while (*p && isspace((unsigned char)*p)) p++;

    int consec = 0;
    while (*p) {
        if (isupper((unsigned char)*p)) {
            consec++;
            if (consec >= 2) return 1;
            /* Skip to end of word */
            while (*p && !isspace((unsigned char)*p)) p++;
        } else {
            consec = 0;
            while (*p && !isspace((unsigned char)*p)) p++;
        }
        while (*p && isspace((unsigned char)*p)) p++;
    }
    return 0;
}

/* Check if a sentence is a question. */
static int is_question(const char *text) {
    size_t len = strlen(text);
    if (len > 0 && text[len - 1] == '?') return 1;
    /* Check for question words at start */
    const char *qwords[] = {"what ", "why ", "how ", "when ", "where ", "who ", NULL};
    char first[8];
    size_t copy = len < 7 ? len : 7;
    for (size_t i = 0; i < copy; i++) {
        first[i] = (char)tolower((unsigned char)text[i]);
    }
    first[copy] = '\0';
    for (int i = 0; qwords[i]; i++) {
        if (strncmp(first, qwords[i], strlen(qwords[i])) == 0) return 1;
    }
    return 0;
}

/* ── TF-IDF Scoring ────────────────────────────────────────────────── */

TFIDFResult tfidf_score(const Sentence *sentences, int count) {
    TFIDFResult result;
    memset(&result, 0, sizeof(result));
    result.count = count;

    if (count == 0 || !sentences) return result;

    /* Initialize hash maps for global TF and document frequency */
    hm_init(&result.tf_global, 1024);
    hm_init(&result.df, 1024);

    /* Allocate scored array */
    result.scored = (ScoredSentence *)calloc((size_t)count, sizeof(ScoredSentence));
    if (!result.scored) return result;

    /* Phase 1: Tokenize all sentences, build TF and DF maps */
    for (int i = 0; i < count; i++) {
        result.scored[i].index = i;
        int tc = 0;
        char **toks = tokenize(sentences[i].text, &tc);
        result.scored[i].tokens      = toks;
        result.scored[i].token_count = tc;

        /* Track which terms appeared in this sentence (for DF) */
        HashMap seen;
        hm_init(&seen, 64);

        for (int j = 0; j < tc; j++) {
            /* Global term frequency: how often this term appears total */
            hm_inc(&result.tf_global, toks[j], 1.0);

            /* Document frequency: in how many sentences does this term appear */
            if (!hm_get(&seen, toks[j])) {
                hm_inc(&result.df, toks[j], 1.0);
                hm_set(&seen, toks[j], 1.0);
            }
        }
        hm_free(&seen);
    }

    double N = (double)count;

    /* Phase 2: Score each sentence */
    double max_score = 0.0;

    for (int i = 0; i < count; i++) {
        ScoredSentence *ss = &result.scored[i];
        double score = 0.0;

        for (int j = 0; j < ss->token_count; j++) {
            HMEntry *tf_entry = hm_get(&result.tf_global, ss->tokens[j]);
            HMEntry *df_entry = hm_get(&result.df, ss->tokens[j]);
            if (!tf_entry || !df_entry) continue;

            double tf  = tf_entry->value;
            double df  = df_entry->value;
            /* IDF = log((N + 1) / (df + 1)) + 1 */
            double idf = log((N + 1.0) / (df + 1.0)) + 1.0;
            score += tf * idf;
        }

        /* Length normalization: divide by sqrt(token_count) */
        if (ss->token_count > 0) {
            score /= sqrt((double)ss->token_count);
        }

        /* Apply bonus/penalty multipliers */
        const char *text = sentences[i].text;

        /* +15% for definition patterns */
        if (has_definition_pattern(text)) {
            score *= 1.15;
        }

        /* +10% for numbers/dates/formulas */
        if (has_numbers(text)) {
            score *= 1.10;
        }

        /* +10% if in the first 15% of text */
        if (sentences[i].original_index < (int)(N * 0.15)) {
            score *= 1.10;
        }

        /* +10% for capitalized multi-word terms */
        if (has_capitalized_phrase(text)) {
            score *= 1.10;
        }

        /* -20% for questions */
        if (is_question(text)) {
            score *= 0.80;
        }

        ss->score = score;
        if (score > max_score) max_score = score;
    }

    /* Phase 3: Normalize scores to 0..1 */
    for (int i = 0; i < count; i++) {
        if (max_score > 0.0) {
            result.scored[i].norm_score = result.scored[i].score / max_score;
        } else {
            result.scored[i].norm_score = 0.0;
        }
    }

    return result;
}

/* Free all memory held by a TFIDFResult. */
void tfidf_result_free(TFIDFResult *r) {
    if (r->scored) {
        for (int i = 0; i < r->count; i++) {
            if (r->scored[i].tokens) {
                for (int j = 0; j < r->scored[i].token_count; j++) {
                    free(r->scored[i].tokens[j]);
                }
                free(r->scored[i].tokens);
            }
        }
        free(r->scored);
        r->scored = NULL;
    }
    hm_free(&r->tf_global);
    hm_free(&r->df);
    r->count = 0;
}
