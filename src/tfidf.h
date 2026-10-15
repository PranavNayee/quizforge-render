/*
 * tfidf.h — Tokenizer, stopwords, suffix stemmer, and TF-IDF sentence scoring.
 */
#ifndef TFIDF_H
#define TFIDF_H

#include "text_clean.h"
#include "util.h"

/* A scored sentence with its metadata. */
typedef struct {
    int     index;          /* Index into the CleanResult.sentences array */
    double  score;          /* Raw TF-IDF score (before normalization) */
    double  norm_score;     /* Normalized 0..1 score */
    char  **tokens;         /* Stemmed tokens (for keyword extraction) */
    int     token_count;    /* Number of tokens */
} ScoredSentence;

/* Result of TF-IDF scoring over all sentences. */
typedef struct {
    ScoredSentence *scored;
    int              count;
    HashMap          tf_global;     /* term → global term frequency */
    HashMap          df;            /* term → document (sentence) frequency */
} TFIDFResult;

/* Score all sentences using TF-IDF with bonus/penalty adjustments.
 * Caller must call tfidf_result_free() to release memory.
 */
TFIDFResult tfidf_score(const Sentence *sentences, int count);

/* Free all memory held by a TFIDFResult. */
void tfidf_result_free(TFIDFResult *r);

/* Tokenize a sentence: lowercase, remove stopwords, apply stemmer.
 * Returns an array of stemmed tokens; sets *out_count.
 * Caller must free each token and the array.
 */
char **tokenize(const char *text, int *out_count);

/* Check if a word is a stopword. */
int is_stopword(const char *word);

/* Apply simple suffix stemmer to a word (in place, modifies buf). */
void stem_word(char *word);

/* Map a stemmed term (or space-separated stemmed phrase) back to the
 * first matching original lowercase word(s) found in the sentences,
 * e.g. "photosynthesi" -> "photosynthesis". Falls back to the input.
 * Returns a newly allocated string; caller must free().
 */
char *stem_to_surface(const char *stem_term, const Sentence *sentences, int count);

#endif /* TFIDF_H */
