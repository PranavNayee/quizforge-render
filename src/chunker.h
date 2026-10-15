/*
 * chunker.h — Group top-ranked sentences into context-rich chunks.
 */
#ifndef CHUNKER_H
#define CHUNKER_H

#include "text_clean.h"
#include "tfidf.h"

/* A chunk is a group of 2-3 neighboring top sentences. */
typedef struct {
    int     id;
    char   *text;              /* Concatenated sentence texts */
    double  score;             /* Average score of contained sentences */
    char   *keywords[3];       /* Top 3 keywords from these sentences */
    int     keyword_count;
    int    *sentence_indices;  /* Which sentences (by index) are in this chunk */
    int     sentence_count;
} Chunk;

typedef struct {
    Chunk *chunks;
    int    count;
} ChunkResult;

/* Pick the top-K sentences, restore order, group into chunks of 2-3.
 * K defaults to 12 if top_k <= 0.
 * Caller must call chunk_result_free() to release memory.
 */
ChunkResult chunk_sentences(const Sentence *sentences, int sent_count,
                            const TFIDFResult *tfidf, int top_k);

/* Free all memory held by a ChunkResult. */
void chunk_result_free(ChunkResult *cr);

#endif /* CHUNKER_H */
