/*
 * chunker.c — Group top-ranked sentences into context-rich chunks.
 *
 * Algorithm:
 *   1. Sort sentences by TF-IDF score descending, pick top K.
 *   2. Re-sort the picked sentences by their original_index (document order).
 *   3. Greedily group consecutive (or near-consecutive) sentences into
 *      chunks of 2-3, so each chunk has enough context for a question.
 *   4. For each chunk, extract the top 3 keywords by TF-IDF weight.
 */
#include "chunker.h"
#include "util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Comparison function: sort by score descending. */
static int cmp_score_desc(const void *a, const void *b) {
    const int *ia = (const int *)a;
    const int *ib = (const int *)b;
    /* We'll sort indices, so we need the scored array via a static pointer */
    /* Instead, use a simple struct */
    (void)ia; (void)ib;
    return 0; /* Placeholder — we'll use a different approach */
}

/* Sort helper struct */
typedef struct {
    int    sent_idx;
    double score;
    int    orig_order;
} PickedSent;

static int cmp_picked_score(const void *a, const void *b) {
    const PickedSent *pa = (const PickedSent *)a;
    const PickedSent *pb = (const PickedSent *)b;
    if (pa->score > pb->score) return -1;
    if (pa->score < pb->score) return 1;
    return 0;
}

static int cmp_picked_order(const void *a, const void *b) {
    const PickedSent *pa = (const PickedSent *)a;
    const PickedSent *pb = (const PickedSent *)b;
    return pa->orig_order - pb->orig_order;
}

/* Extract top keywords from a set of sentences' tokens. */
static void extract_chunk_keywords(const TFIDFResult *tfidf,
                                   const int *sent_indices, int sent_count,
                                   char *keywords[3], int *kw_count) {
    *kw_count = 0;
    HashMap kw_scores;
    hm_init(&kw_scores, 128);

    for (int s = 0; s < sent_count; s++) {
        int idx = sent_indices[s];
        if (idx < 0 || idx >= tfidf->count) continue;
        const ScoredSentence *ss = &tfidf->scored[idx];
        for (int t = 0; t < ss->token_count; t++) {
            const char *tok = ss->tokens[t];
            HMEntry *tf_e = hm_get((HashMap *)&tfidf->tf_global, tok);
            HMEntry *df_e = hm_get((HashMap *)&tfidf->df, tok);
            if (!tf_e || !df_e) continue;
            double N = (double)tfidf->count;
            double idf = log((N + 1.0) / (df_e->value + 1.0)) + 1.0;
            double w   = tf_e->value * idf;
            hm_inc(&kw_scores, tok, w);
        }
    }

    /* Find top 3 by weight */
    typedef struct { const char *term; double w; } KW;
    KW top[3] = {{NULL,0},{NULL,0},{NULL,0}};

    for (size_t b = 0; b < kw_scores.n_buckets; b++) {
        HMEntry *e = kw_scores.buckets[b];
        while (e) {
            double w = e->value;
            /* Insert into top-3 */
            for (int k = 0; k < 3; k++) {
                if (w > top[k].w) {
                    /* Shift down */
                    for (int m = 2; m > k; m--) {
                        top[m] = top[m-1];
                    }
                    top[k].term = e->key;
                    top[k].w = w;
                    break;
                }
            }
            e = e->next;
        }
    }

    for (int k = 0; k < 3; k++) {
        if (top[k].term) {
            keywords[k] = safe_strdup(top[k].term);
            (*kw_count)++;
        } else {
            keywords[k] = NULL;
        }
    }

    hm_free(&kw_scores);
}

ChunkResult chunk_sentences(const Sentence *sentences, int sent_count,
                            const TFIDFResult *tfidf, int top_k) {
    ChunkResult cr;
    memset(&cr, 0, sizeof(cr));

    if (!sentences || sent_count <= 0 || !tfidf || !tfidf->scored) return cr;
    if (top_k <= 0) top_k = 12;
    if (top_k > sent_count) top_k = sent_count;

    /* Step 1: Pick top K sentences by score */
    PickedSent *picked = (PickedSent *)malloc(sizeof(PickedSent) * (size_t)sent_count);
    if (!picked) return cr;

    for (int i = 0; i < sent_count; i++) {
        picked[i].sent_idx   = i;
        picked[i].score      = tfidf->scored[i].score;
        picked[i].orig_order = sentences[i].original_index;
    }

    /* Sort by score descending and take top K */
    qsort(picked, (size_t)sent_count, sizeof(PickedSent), cmp_picked_score);

    /* Keep only top K */
    int k = top_k;

    /* Step 2: Re-sort the top K by original document order */
    qsort(picked, (size_t)k, sizeof(PickedSent), cmp_picked_order);

    /* Step 3: Group into chunks of 2-3 consecutive sentences.
     * "Consecutive" means their original indices differ by at most 2. */
    int max_chunks = k; /* worst case: every picked sentence is its own chunk */
    cr.chunks = (Chunk *)calloc((size_t)max_chunks, sizeof(Chunk));
    if (!cr.chunks) { free(picked); return cr; }

    int chunk_id = 0;
    int i = 0;
    while (i < k) {
        /* Start a new chunk with sentence i */
        int group_start = i;
        int group_end   = i;

        /* Try to extend the chunk to 2-3 consecutive sentences */
        while (group_end - group_start < 2 && group_end + 1 < k) {
            int cur_orig  = picked[group_end].orig_order;
            int next_orig = picked[group_end + 1].orig_order;
            /* Group if they're within 2 positions of each other */
            if (next_orig - cur_orig <= 2) {
                group_end++;
            } else {
                break;
            }
        }

        int group_size = group_end - group_start + 1;

        /* Build the chunk */
        Chunk *c = &cr.chunks[chunk_id];
        c->id = chunk_id;
        c->sentence_count   = group_size;
        c->sentence_indices = (int *)malloc(sizeof(int) * (size_t)group_size);

        StrBuf text;
        strbuf_init(&text, 512);
        double total_score = 0.0;

        for (int j = group_start; j <= group_end; j++) {
            int si = picked[j].sent_idx;
            if (c->sentence_indices) {
                c->sentence_indices[j - group_start] = si;
            }
            if (j > group_start) strbuf_appends(&text, " ");
            strbuf_appends(&text, sentences[si].text);
            total_score += picked[j].score;
        }

        c->text  = text.data; /* Transfer ownership */
        c->score = total_score / (double)group_size;

        /* Extract keywords */
        if (c->sentence_indices) {
            extract_chunk_keywords(tfidf, c->sentence_indices, group_size,
                                   c->keywords, &c->keyword_count);
            /* Show readable words instead of stems */
            for (int kw = 0; kw < c->keyword_count; kw++) {
                if (c->keywords[kw]) {
                    char *surf = stem_to_surface(c->keywords[kw], sentences, sent_count);
                    free(c->keywords[kw]);
                    c->keywords[kw] = surf;
                }
            }
        }

        chunk_id++;
        i = group_end + 1;
    }

    cr.count = chunk_id;
    free(picked);

    /* Suppress unused function warning */
    (void)cmp_score_desc;

    return cr;
}

void chunk_result_free(ChunkResult *cr) {
    if (!cr->chunks) return;
    for (int i = 0; i < cr->count; i++) {
        free(cr->chunks[i].text);
        for (int k = 0; k < 3; k++) {
            free(cr->chunks[i].keywords[k]);
        }
        free(cr->chunks[i].sentence_indices);
    }
    free(cr->chunks);
    cr->chunks = NULL;
    cr->count  = 0;
}
