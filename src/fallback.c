/*
 * fallback.c — Offline quiz generation (no AI required).
 *
 * Cloze (fill-in-the-blank) MCQs:
 *   Pick a top-scored sentence, find its highest-weight key term,
 *   replace it with "______", and build 3 distractors from other key terms.
 *
 * Flashcards:
 *   Identify definition sentences (contain "is defined as", "is a", etc.),
 *   use the subject as the front and the definition as the back.
 */
#include "fallback.h"
#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Case-insensitive strstr. */
static const char *strcasestr_safe(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return NULL;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) return haystack + i;
    }
    return NULL;
}

/* Case-insensitive whole-word search (match must not touch letters/digits). */
static const char *find_word(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return NULL;
    const char *p = haystack;
    while ((p = strcasestr_safe(p, needle)) != NULL) {
        int left_ok  = (p == haystack) || !isalnum((unsigned char)p[-1]);
        int right_ok = !isalnum((unsigned char)p[nlen]);
        if (left_ok && right_ok) return p;
        p++;
    }
    return NULL;
}

/* Find the highest-weight key term in a sentence that was not used before. */
static int find_best_term_in_sentence(const char *sentence,
                                      const KeyTermResult *kts,
                                      const char *used) {
    int best = -1;
    double best_w = -1.0;
    for (int i = 0; i < kts->count; i++) {
        if (used && used[i]) continue;
        if (find_word(sentence, kts->terms[i].term) &&
            kts->terms[i].weight > best_w) {
            best_w = kts->terms[i].weight;
            best = i;
        }
    }
    return best;
}

/* Create a cloze question by blanking out every occurrence of a key term. */
static cJSON *make_cloze_mcq(const char *sentence, int term_idx,
                              const KeyTermResult *kts, int chunk_id) {
    const char *term = kts->terms[term_idx].term;
    size_t tlen = strlen(term);
    if (!find_word(sentence, term)) return NULL;

    /* Pick 3 distractors that do NOT appear in the sentence (no hints/leaks)
     * and do not overlap with the answer text. */
    char *options[4];
    int opt_count = 1;
    options[0] = safe_strdup(term);
    for (int i = 0; i < kts->count && opt_count < 4; i++) {
        if (i == term_idx) continue;
        const char *cand = kts->terms[i].term;
        if (find_word(sentence, cand)) continue;
        if (strcasestr_safe(cand, term) || strcasestr_safe(term, cand)) continue;
        options[opt_count++] = safe_strdup(cand);
    }
    if (opt_count < 4) {           /* not enough clean distractors: skip */
        for (int i = 0; i < opt_count; i++) free(options[i]);
        return NULL;
    }

    /* Build cloze text with all occurrences blanked */
    StrBuf cloze;
    strbuf_init(&cloze, strlen(sentence) + 32);
    const char *cur = sentence;
    const char *pos;
    while ((pos = find_word(cur, term)) != NULL) {
        strbuf_append(&cloze, cur, (size_t)(pos - cur));
        strbuf_appends(&cloze, "______");
        cur = pos + tlen;
    }
    strbuf_appends(&cloze, cur);

    StrBuf question;
    strbuf_init(&question, cloze.len + 64);
    strbuf_appendf(&question, "Fill in the blank: %s", cloze.data);
    strbuf_free(&cloze);

    /* Shuffle options (Fisher-Yates), track correct answer */
    int correct_idx = 0;
    for (int i = 3; i > 0; i--) {
        int j = rand() % (i + 1);
        char *tmp = options[i];
        options[i] = options[j];
        options[j] = tmp;
        if (correct_idx == i) correct_idx = j;
        else if (correct_idx == j) correct_idx = i;
    }

    cJSON *mcq = cJSON_CreateObject();
    cJSON_AddStringToObject(mcq, "question", question.data);
    strbuf_free(&question);

    cJSON *opts = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON_AddItemToArray(opts, cJSON_CreateString(options[i]));
        free(options[i]);
    }
    cJSON_AddItemToObject(mcq, "options", opts);
    cJSON_AddNumberToObject(mcq, "answer_index", correct_idx);

    StrBuf expl;
    strbuf_init(&expl, 256);
    strbuf_appendf(&expl, "The correct answer is \"%s\". Source: %s", term, sentence);
    cJSON_AddStringToObject(mcq, "explanation", expl.data);
    strbuf_free(&expl);

    cJSON_AddNumberToObject(mcq, "chunk_id", chunk_id);
    return mcq;
}

/* Check if a sentence is a definition. */
static int is_definition_sentence(const char *text) {
    const char *patterns[] = {
        "is defined as", "refers to", "is called", " means ",
        " is a ", " is an ", " is the ", "known as",
        NULL
    };
    for (int i = 0; patterns[i]; i++) {
        if (strcasestr_safe(text, patterns[i])) return 1;
    }
    return 0;
}

/* Extract the subject (first few words before the definition pattern). */
static char *extract_subject(const char *text) {
    const char *patterns[] = {
        " is defined as", " refers to", " is called", " means ",
        " is a ", " is an ", " is the ", " known as",
        NULL
    };
    const char *earliest = NULL;
    for (int i = 0; patterns[i]; i++) {
        const char *p = strcasestr_safe(text, patterns[i]);
        if (p && (!earliest || p < earliest)) {
            earliest = p;
        }
    }
    if (!earliest || earliest == text) return safe_strdup("Term");

    size_t len = (size_t)(earliest - text);
    char *subject = (char *)malloc(len + 1);
    if (!subject) return safe_strdup("Term");
    memcpy(subject, text, len);
    subject[len] = '\0';

    /* Trim */
    size_t slen = strlen(subject);
    while (slen > 0 && isspace((unsigned char)subject[slen - 1])) {
        subject[--slen] = '\0';
    }

    return subject;
}

cJSON *fallback_generate(const Sentence *sentences, int sent_count,
                         const TFIDFResult *tfidf,
                         const ChunkResult *chunks,
                         const KeyTermResult *key_terms,
                         int num_questions, const char *mode) {
    cJSON *result = cJSON_CreateObject();
    if (!result) return NULL;

    int want_mcq = !mode || strcmp(mode, "flashcards") != 0;
    int want_fc  = !mode || strcmp(mode, "mcq") != 0;

    /* Generate MCQs from top-scored sentences */
    cJSON *mcqs = cJSON_CreateArray();

    if (want_mcq && key_terms && key_terms->count >= 2) {
        /* Sort sentences by score to pick top ones */
        typedef struct { int idx; double score; } SI;
        SI *sorted = (SI *)malloc(sizeof(SI) * (size_t)sent_count);
        if (sorted) {
            for (int i = 0; i < sent_count; i++) {
                sorted[i].idx = i;
                sorted[i].score = tfidf ? tfidf->scored[i].score : 0.0;
            }
            /* Simple sort by score descending */
            for (int i = 0; i < sent_count - 1; i++) {
                for (int j = i + 1; j < sent_count; j++) {
                    if (sorted[j].score > sorted[i].score) {
                        SI tmp = sorted[i];
                        sorted[i] = sorted[j];
                        sorted[j] = tmp;
                    }
                }
            }

            int generated = 0;
            char *used = (char *)calloc((size_t)key_terms->count, 1);
            for (int i = 0; used && i < sent_count && generated < num_questions; i++) {
                int si = sorted[i].idx;
                int term_idx = find_best_term_in_sentence(
                    sentences[si].text, key_terms, used);
                if (term_idx >= 0) {
                    /* Find which chunk this sentence belongs to */
                    int cid = 0;
                    if (chunks) {
                        for (int c = 0; c < chunks->count; c++) {
                            for (int s = 0; s < chunks->chunks[c].sentence_count; s++) {
                                if (chunks->chunks[c].sentence_indices &&
                                    chunks->chunks[c].sentence_indices[s] == si) {
                                    cid = c;
                                }
                            }
                        }
                    }
                    cJSON *mcq = make_cloze_mcq(sentences[si].text,
                                                 term_idx, key_terms, cid);
                    if (mcq) {
                        cJSON_AddItemToArray(mcqs, mcq);
                        used[term_idx] = 1;
                        generated++;
                    }
                }
            }
            free(used);
            free(sorted);
        }
    }
    cJSON_AddItemToObject(result, "mcqs", mcqs);

    /* Generate flashcards from definition sentences */
    cJSON *flashcards = cJSON_CreateArray();

    if (want_fc) {
        int fc_count = 0;
        int max_fc = num_questions > 0 ? num_questions : 8;

        for (int i = 0; i < sent_count && fc_count < max_fc; i++) {
            if (is_definition_sentence(sentences[i].text)) {
                char *subject = extract_subject(sentences[i].text);
                cJSON *card = cJSON_CreateObject();
                cJSON_AddStringToObject(card, "front", subject);
                cJSON_AddStringToObject(card, "back", sentences[i].text);
                cJSON_AddItemToArray(flashcards, card);
                free(subject);
                fc_count++;
            }
        }

        /* If we didn't find enough definitions, use top sentences as cards */
        if (fc_count < max_fc && tfidf && tfidf->scored) {
            /* Use top-scored non-definition sentences */
            typedef struct { int idx; double score; } SI2;
            SI2 *sorted = (SI2 *)malloc(sizeof(SI2) * (size_t)sent_count);
            if (sorted) {
                for (int i = 0; i < sent_count; i++) {
                    sorted[i].idx = i;
                    sorted[i].score = tfidf->scored[i].score;
                }
                for (int i = 0; i < sent_count - 1; i++) {
                    for (int j = i + 1; j < sent_count; j++) {
                        if (sorted[j].score > sorted[i].score) {
                            SI2 tmp = sorted[i];
                            sorted[i] = sorted[j];
                            sorted[j] = tmp;
                        }
                    }
                }
                for (int i = 0; i < sent_count && fc_count < max_fc; i++) {
                    int si = sorted[i].idx;
                    if (!is_definition_sentence(sentences[si].text)) {
                        /* Find a key term in the sentence for the front */
                        int tidx = find_best_term_in_sentence(
                            sentences[si].text, key_terms, NULL);
                        const char *front = (tidx >= 0) ?
                            key_terms->terms[tidx].term : "Key Concept";
                        cJSON *card = cJSON_CreateObject();
                        cJSON_AddStringToObject(card, "front", front);
                        cJSON_AddStringToObject(card, "back", sentences[si].text);
                        cJSON_AddItemToArray(flashcards, card);
                        fc_count++;
                    }
                }
                free(sorted);
            }
        }
    }
    cJSON_AddItemToObject(result, "flashcards", flashcards);

    return result;
}
