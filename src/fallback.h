/*
 * fallback.h — Offline quiz generation (no AI required).
 */
#ifndef FALLBACK_H
#define FALLBACK_H

#include "chunker.h"
#include "keyterms.h"
#include "text_clean.h"
#include "tfidf.h"
#include "cJSON.h"

/* Generate offline MCQs and flashcards from analyzed text data.
 * Returns a cJSON object with "mcqs" and "flashcards" arrays.
 * Caller must cJSON_Delete() the returned object.
 */
cJSON *fallback_generate(const Sentence *sentences, int sent_count,
                         const TFIDFResult *tfidf,
                         const ChunkResult *chunks,
                         const KeyTermResult *key_terms,
                         int num_questions, const char *mode);

#endif /* FALLBACK_H */
