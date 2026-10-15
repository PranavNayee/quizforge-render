/*
 * keyterms.h — Key term extraction from scored sentences.
 */
#ifndef KEYTERMS_H
#define KEYTERMS_H

#include "tfidf.h"
#include "text_clean.h"

/* A single key term with its normalized weight. */
typedef struct {
    char  *term;
    double weight;   /* 0.0 to 1.0 */
} KeyTerm;

typedef struct {
    KeyTerm *terms;
    int      count;
} KeyTermResult;

/* Extract the top key terms (up to max_terms) from scored sentences.
 * Detects capitalized phrases and repeated bigrams.
 * Caller must call keyterm_result_free().
 */
KeyTermResult extract_key_terms(const Sentence *sentences, int sent_count,
                                const TFIDFResult *tfidf, int max_terms);

/* Free all memory held by a KeyTermResult. */
void keyterm_result_free(KeyTermResult *r);

#endif /* KEYTERMS_H */
