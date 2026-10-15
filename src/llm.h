/*
 * llm.h — LLM client for OpenAI-compatible chat completions via libcurl.
 */
#ifndef LLM_H
#define LLM_H

#include "chunker.h"
#include "keyterms.h"
#include "cJSON.h"

/* Call the LLM API to generate MCQs and/or flashcards.
 * Returns a cJSON object with "mcqs" and "flashcards" arrays, or NULL on failure.
 * Caller must cJSON_Delete() the returned object.
 */
cJSON *llm_generate(const ChunkResult *chunks,
                    const KeyTermResult *key_terms,
                    int num_questions,
                    const char *difficulty,
                    const char *mode);

/* Check whether an LLM API key is configured. */
int llm_available(void);

#endif /* LLM_H */
