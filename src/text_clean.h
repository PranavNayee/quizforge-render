/*
 * text_clean.h — Text normalization and sentence splitting.
 */
#ifndef TEXT_CLEAN_H
#define TEXT_CLEAN_H

#include <stddef.h>

/* A single sentence extracted from the input text. */
typedef struct {
    char  *text;           /* Original (cleaned) sentence text */
    int    original_index; /* Position in the source text (for ordering) */
    int    word_count;     /* Number of whitespace-delimited words */
} Sentence;

/* Result of text cleaning and splitting. */
typedef struct {
    Sentence *sentences;
    int        count;
    int        total_words;  /* Total words in the cleaned text */
    char      *cleaned_text; /* Full cleaned text (for heatmap display) */
} CleanResult;

/* Clean and split input text into sentences.
 * Normalizes whitespace, strips control chars, removes bullets,
 * collapses repeated newlines, and splits on sentence boundaries.
 * Drops sentences with < 5 or > 60 words.
 * Caller must call clean_result_free() to release memory.
 */
CleanResult text_clean_and_split(const char *input, size_t input_len);

/* Free all memory held by a CleanResult. */
void clean_result_free(CleanResult *cr);

#endif /* TEXT_CLEAN_H */
