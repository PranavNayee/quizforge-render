/*
 * keyterms.c — Key term extraction with TF-IDF ranking, capitalized
 *              phrase detection, and bigram support.
 *
 * Approach:
 *   1. Collect all stemmed unigrams with their TF-IDF weights.
 *   2. Scan original text for capitalized multi-word phrases and repeated
 *      bigrams (e.g., "machine learning"), boosting their weight.
 *   3. Return the top max_terms terms with weights normalized to 0..1.
 */
#include "keyterms.h"
#include "util.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Collect entry for sorting. */
typedef struct {
    const char *term;
    double      weight;
} TermEntry;

static int cmp_term_weight(const void *a, const void *b) {
    const TermEntry *ta = (const TermEntry *)a;
    const TermEntry *tb = (const TermEntry *)b;
    if (ta->weight > tb->weight) return -1;
    if (ta->weight < tb->weight) return 1;
    return 0;
}

/* Detect capitalized multi-word phrases in the original text. */
static void detect_capitalized_phrases(const Sentence *sentences, int count,
                                       HashMap *phrase_map) {
    for (int i = 0; i < count; i++) {
        const char *p = sentences[i].text;
        /* Skip the first word (always capitalized at sentence start) */
        while (*p && !isspace((unsigned char)*p)) p++;
        while (*p && isspace((unsigned char)*p)) p++;

        char phrase[256];
        int plen = 0;
        int word_count = 0;

        while (*p) {
            if (isupper((unsigned char)*p)) {
                /* Start or continue a capitalized phrase */
                if (plen > 0 && plen < 254) {
                    phrase[plen++] = ' ';
                }
                while (*p && !isspace((unsigned char)*p) && plen < 254) {
                    phrase[plen++] = (char)tolower((unsigned char)*p);
                    p++;
                }
                word_count++;
            } else {
                /* End of capitalized phrase */
                if (word_count >= 2 && plen > 0) {
                    phrase[plen] = '\0';
                    hm_inc(phrase_map, phrase, 2.0); /* Boost weight */
                }
                plen = 0;
                word_count = 0;
                while (*p && !isspace((unsigned char)*p)) p++;
            }
            while (*p && isspace((unsigned char)*p)) p++;
        }
        /* Flush any trailing phrase */
        if (word_count >= 2 && plen > 0) {
            phrase[plen] = '\0';
            hm_inc(phrase_map, phrase, 2.0);
        }
    }
}

/* Detect repeated bigrams from tokenized sentences. */
static void detect_bigrams(const TFIDFResult *tfidf, HashMap *bigram_map) {
    for (int i = 0; i < tfidf->count; i++) {
        const ScoredSentence *ss = &tfidf->scored[i];
        for (int j = 0; j + 1 < ss->token_count; j++) {
            char bigram[256];
            int n = snprintf(bigram, sizeof(bigram), "%s %s",
                             ss->tokens[j], ss->tokens[j + 1]);
            if (n > 0 && (size_t)n < sizeof(bigram)) {
                hm_inc_int(bigram_map, bigram, 1);
            }
        }
    }
}

KeyTermResult extract_key_terms(const Sentence *sentences, int sent_count,
                                const TFIDFResult *tfidf, int max_terms) {
    KeyTermResult result;
    memset(&result, 0, sizeof(result));

    if (!tfidf || !tfidf->scored || tfidf->count == 0) return result;
    if (max_terms <= 0) max_terms = 12;

    double N = (double)tfidf->count;

    /* Step 1: Compute TF-IDF weight for all unigrams */
    HashMap term_weights;
    hm_init(&term_weights, 512);

    for (size_t b = 0; b < tfidf->tf_global.n_buckets; b++) {
        HMEntry *e = tfidf->tf_global.buckets[b];
        while (e) {
            HMEntry *df_entry = hm_get((HashMap *)&tfidf->df, e->key);
            double df = df_entry ? df_entry->value : 1.0;
            double idf = log((N + 1.0) / (df + 1.0)) + 1.0;
            double w = e->value * idf;
            hm_set(&term_weights, e->key, w);
            e = e->next;
        }
    }

    /* Step 2: Detect and add capitalized phrases */
    HashMap phrase_map;
    hm_init(&phrase_map, 256);
    detect_capitalized_phrases(sentences, sent_count, &phrase_map);

    /* Merge phrases into term_weights with a boost */
    for (size_t b = 0; b < phrase_map.n_buckets; b++) {
        HMEntry *e = phrase_map.buckets[b];
        while (e) {
            HMEntry *existing = hm_get(&term_weights, e->key);
            double base = existing ? existing->value : 0.0;
            hm_set(&term_weights, e->key, base + e->value * 1.5);
            e = e->next;
        }
    }
    hm_free(&phrase_map);

    /* Step 3: Detect repeated bigrams */
    HashMap bigram_map;
    hm_init(&bigram_map, 256);
    detect_bigrams(tfidf, &bigram_map);

    /* Add bigrams that appear 2+ times */
    for (size_t b = 0; b < bigram_map.n_buckets; b++) {
        HMEntry *e = bigram_map.buckets[b];
        while (e) {
            if (e->int_val >= 2) {
                HMEntry *existing = hm_get(&term_weights, e->key);
                double base = existing ? existing->value : 0.0;
                hm_set(&term_weights, e->key, base + (double)e->int_val * 1.5);
            }
            e = e->next;
        }
    }
    hm_free(&bigram_map);

    /* Step 4: Sort all terms by weight and pick top N */
    size_t total = hm_count(&term_weights);
    if (total == 0) { hm_free(&term_weights); return result; }

    TermEntry *entries = (TermEntry *)malloc(sizeof(TermEntry) * total);
    if (!entries) { hm_free(&term_weights); return result; }

    size_t idx = 0;
    for (size_t b = 0; b < term_weights.n_buckets; b++) {
        HMEntry *e = term_weights.buckets[b];
        while (e) {
            entries[idx].term   = e->key;
            entries[idx].weight = e->value;
            idx++;
            e = e->next;
        }
    }

    qsort(entries, idx, sizeof(TermEntry), cmp_term_weight);

    /* Take top max_terms */
    int n = (int)idx < max_terms ? (int)idx : max_terms;
    result.terms = (KeyTerm *)malloc(sizeof(KeyTerm) * (size_t)n);
    if (!result.terms) { free(entries); hm_free(&term_weights); return result; }

    /* Normalize weights to 0..1 */
    double max_w = entries[0].weight;

    for (int i = 0; i < n; i++) {
        result.terms[i].term = stem_to_surface(entries[i].term, sentences, sent_count);
        result.terms[i].weight = max_w > 0.0 ? entries[i].weight / max_w : 0.0;
    }
    result.count = n;

    free(entries);
    hm_free(&term_weights);

    return result;
}

void keyterm_result_free(KeyTermResult *r) {
    if (r->terms) {
        for (int i = 0; i < r->count; i++) {
            free(r->terms[i].term);
        }
        free(r->terms);
        r->terms = NULL;
    }
    r->count = 0;
}
