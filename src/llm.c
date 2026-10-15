/*
 * llm.c — LLM client using libcurl for OpenAI-compatible chat completions.
 *
 * Builds a single prompt containing the top chunks, key terms, difficulty,
 * and question count. Parses the JSON response with cJSON. Falls back
 * gracefully on any error (timeout, bad JSON, missing key).
 */
#include "llm.h"
#include "util.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default configuration values. */
#define DEFAULT_API_URL  "https://integrate.api.nvidia.com/v1/chat/completions"
#define DEFAULT_MODEL    "nvidia/llama-3.1-nemotron-70b-instruct"
#define LLM_TIMEOUT_SECS 30

/* ── cURL write callback ───────────────────────────────────────────── */

static size_t write_callback(char *ptr, size_t size, size_t nmemb,
                              void *userdata) {
    StrBuf *sb = (StrBuf *)userdata;
    size_t total = size * nmemb;
    strbuf_append(sb, ptr, total);
    return total;
}

/* ── Configuration helpers ─────────────────────────────────────────── */

/* Get the API key from environment, or NULL if not set. */
static const char *get_api_key(void) {
    const char *key = getenv("LLM_API_KEY");
    if (key && key[0] == '\0') return NULL;
    return key;
}

/* Get the API URL from environment, with a default. */
static const char *get_api_url(void) {
    const char *url = getenv("LLM_API_URL");
    if (url && url[0] != '\0') return url;
    return DEFAULT_API_URL;
}

/* Get the model name from environment, with a default. */
static const char *get_model(void) {
    const char *model = getenv("LLM_MODEL");
    if (model && model[0] != '\0') return model;
    return DEFAULT_MODEL;
}

/* Check whether an LLM API key is configured. */
int llm_available(void) {
    return get_api_key() != NULL;
}

/* ── Prompt builder ────────────────────────────────────────────────── */

/* Build the system prompt that enforces strict JSON output. */
static char *build_system_prompt(void) {
    return safe_strdup(
        "You are an expert quiz generator for students. "
        "You MUST respond with ONLY a valid JSON object, no markdown fences, "
        "no explanation, no text before or after the JSON. "
        "The JSON must match this exact schema:\n"
        "{\n"
        "  \"mcqs\": [\n"
        "    {\n"
        "      \"question\": \"string\",\n"
        "      \"options\": [\"A\", \"B\", \"C\", \"D\"],\n"
        "      \"answer_index\": 0,\n"
        "      \"explanation\": \"One-sentence explanation\",\n"
        "      \"chunk_id\": 0\n"
        "    }\n"
        "  ],\n"
        "  \"flashcards\": [\n"
        "    {\n"
        "      \"front\": \"Term or question\",\n"
        "      \"back\": \"Definition or answer\"\n"
        "    }\n"
        "  ]\n"
        "}\n"
        "Rules:\n"
        "- Each MCQ must have EXACTLY 4 options.\n"
        "- answer_index is 0-3, indicating the correct option.\n"
        "- Distractors must be plausible but clearly wrong.\n"
        "- explanation is ONE sentence explaining why the answer is correct.\n"
        "- chunk_id references which chunk the question is based on.\n"
        "- Flashcard front is a term or concise question, back is the answer.\n"
        "- Output ONLY the JSON object. No other text."
    );
}

/* Build the user prompt with chunks, key terms, difficulty, and count. */
static char *build_user_prompt(const ChunkResult *chunks,
                                const KeyTermResult *key_terms,
                                int num_questions,
                                const char *difficulty,
                                const char *mode) {
    StrBuf sb;
    strbuf_init(&sb, 4096);

    int want_mcq = !mode || strcmp(mode, "flashcards") != 0;
    int want_fc  = !mode || strcmp(mode, "mcq") != 0;

    strbuf_appendf(&sb,
        "Generate quiz questions from the following study material.\n\n"
        "Difficulty: %s\n",
        difficulty ? difficulty : "medium");

    if (want_mcq && want_fc) {
        strbuf_appendf(&sb,
            "Generate %d MCQs and %d flashcards.\n\n",
            num_questions, num_questions);
    } else if (want_mcq) {
        strbuf_appendf(&sb,
            "Generate %d MCQs. Set flashcards to an empty array.\n\n",
            num_questions);
    } else {
        strbuf_appendf(&sb,
            "Generate %d flashcards. Set mcqs to an empty array.\n\n",
            num_questions);
    }

    strbuf_appends(&sb, "KEY TERMS:\n");
    if (key_terms) {
        for (int i = 0; i < key_terms->count; i++) {
            strbuf_appendf(&sb, "- %s (importance: %.2f)\n",
                           key_terms->terms[i].term,
                           key_terms->terms[i].weight);
        }
    }

    strbuf_appends(&sb, "\nSTUDY MATERIAL CHUNKS:\n\n");
    if (chunks) {
        for (int i = 0; i < chunks->count; i++) {
            strbuf_appendf(&sb, "[Chunk %d] %s\n\n",
                           chunks->chunks[i].id,
                           chunks->chunks[i].text);
        }
    }

    char *result = sb.data;
    sb.data = NULL;
    return result;
}

/* ── JSON extraction / cleanup ─────────────────────────────────────── */

/* Extract a JSON object from text that might have code fences or other wrapping. */
static char *extract_json(const char *text) {
    if (!text) return NULL;

    /* Find the first '{' and the last '}' */
    const char *start = strchr(text, '{');
    if (!start) return NULL;

    const char *end = strrchr(text, '}');
    if (!end || end <= start) return NULL;

    size_t len = (size_t)(end - start + 1);
    char *json = (char *)malloc(len + 1);
    if (!json) return NULL;
    memcpy(json, start, len);
    json[len] = '\0';

    return json;
}

/* Validate an MCQ object; return 1 if valid. */
static int validate_mcq(const cJSON *mcq) {
    if (!cJSON_IsObject(mcq)) return 0;

    const cJSON *q = cJSON_GetObjectItem(mcq, "question");
    if (!cJSON_IsString(q) || !q->valuestring[0]) return 0;

    const cJSON *opts = cJSON_GetObjectItem(mcq, "options");
    if (!cJSON_IsArray(opts) || cJSON_GetArraySize(opts) != 4) return 0;

    for (int i = 0; i < 4; i++) {
        const cJSON *opt = cJSON_GetArrayItem(opts, i);
        if (!cJSON_IsString(opt)) return 0;
    }

    const cJSON *ai = cJSON_GetObjectItem(mcq, "answer_index");
    if (!cJSON_IsNumber(ai)) return 0;
    int idx = ai->valueint;
    if (idx < 0 || idx > 3) return 0;

    const cJSON *expl = cJSON_GetObjectItem(mcq, "explanation");
    if (!cJSON_IsString(expl)) return 0;

    return 1;
}

/* Validate a flashcard object; return 1 if valid. */
static int validate_flashcard(const cJSON *fc) {
    if (!cJSON_IsObject(fc)) return 0;

    const cJSON *front = cJSON_GetObjectItem(fc, "front");
    if (!cJSON_IsString(front) || !front->valuestring[0]) return 0;

    const cJSON *back = cJSON_GetObjectItem(fc, "back");
    if (!cJSON_IsString(back) || !back->valuestring[0]) return 0;

    return 1;
}

/* Parse and validate the LLM response, returning a clean cJSON object. */
static cJSON *parse_llm_response(const char *response_text) {
    char *json_str = extract_json(response_text);
    if (!json_str) return NULL;

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) return NULL;

    /* Build a validated result */
    cJSON *result = cJSON_CreateObject();

    /* Validate MCQs */
    cJSON *mcqs_in = cJSON_GetObjectItem(root, "mcqs");
    cJSON *mcqs_out = cJSON_CreateArray();
    if (cJSON_IsArray(mcqs_in)) {
        int size = cJSON_GetArraySize(mcqs_in);
        for (int i = 0; i < size; i++) {
            cJSON *mcq = cJSON_GetArrayItem(mcqs_in, i);
            if (validate_mcq(mcq)) {
                cJSON_AddItemToArray(mcqs_out, cJSON_Duplicate(mcq, 1));
            }
        }
    }
    cJSON_AddItemToObject(result, "mcqs", mcqs_out);

    /* Validate flashcards */
    cJSON *fcs_in = cJSON_GetObjectItem(root, "flashcards");
    cJSON *fcs_out = cJSON_CreateArray();
    if (cJSON_IsArray(fcs_in)) {
        int size = cJSON_GetArraySize(fcs_in);
        for (int i = 0; i < size; i++) {
            cJSON *fc = cJSON_GetArrayItem(fcs_in, i);
            if (validate_flashcard(fc)) {
                cJSON_AddItemToArray(fcs_out, cJSON_Duplicate(fc, 1));
            }
        }
    }
    cJSON_AddItemToObject(result, "flashcards", fcs_out);

    cJSON_Delete(root);
    return result;
}

/* ── Main LLM call ─────────────────────────────────────────────────── */

cJSON *llm_generate(const ChunkResult *chunks,
                    const KeyTermResult *key_terms,
                    int num_questions,
                    const char *difficulty,
                    const char *mode) {
    const char *api_key = get_api_key();
    if (!api_key) return NULL;

    const char *api_url = get_api_url();
    const char *model   = get_model();

    /* Build prompts */
    char *system_prompt = build_system_prompt();
    char *user_prompt   = build_user_prompt(chunks, key_terms, num_questions,
                                            difficulty, mode);
    if (!system_prompt || !user_prompt) {
        free(system_prompt);
        free(user_prompt);
        return NULL;
    }

    /* Build the request JSON */
    char *sys_escaped  = json_escape(system_prompt);
    char *user_escaped = json_escape(user_prompt);
    free(system_prompt);
    free(user_prompt);

    StrBuf body;
    strbuf_init(&body, 8192);
    strbuf_appendf(&body,
        "{"
        "\"model\":\"%s\","
        "\"messages\":["
        "{\"role\":\"system\",\"content\":\"%s\"},"
        "{\"role\":\"user\",\"content\":\"%s\"}"
        "],"
        "\"temperature\":0.3,"
        "\"max_tokens\":4096"
        "}",
        model, sys_escaped, user_escaped);

    free(sys_escaped);
    free(user_escaped);

    /* Make the HTTP request with libcurl */
    CURL *curl = curl_easy_init();
    if (!curl) {
        strbuf_free(&body);
        return NULL;
    }

    StrBuf response;
    strbuf_init(&response, 4096);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.len);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)LLM_TIMEOUT_SECS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    strbuf_free(&body);

    if (res != CURLE_OK) {
        fprintf(stderr, "LLM request failed: %s\n", curl_easy_strerror(res));
        strbuf_free(&response);
        return NULL;
    }

    /* Parse the OpenAI-style response to extract the content */
    cJSON *api_response = cJSON_Parse(response.data);
    strbuf_free(&response);
    if (!api_response) {
        fprintf(stderr, "LLM: Failed to parse API response JSON\n");
        return NULL;
    }

    /* Extract choices[0].message.content */
    const cJSON *choices = cJSON_GetObjectItem(api_response, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        fprintf(stderr, "LLM: No choices in response\n");
        cJSON_Delete(api_response);
        return NULL;
    }

    const cJSON *first = cJSON_GetArrayItem(choices, 0);
    const cJSON *message = cJSON_GetObjectItem(first, "message");
    const cJSON *content = cJSON_GetObjectItem(message, "content");

    if (!cJSON_IsString(content) || !content->valuestring[0]) {
        fprintf(stderr, "LLM: Empty content in response\n");
        cJSON_Delete(api_response);
        return NULL;
    }

    /* Parse and validate the model's JSON output */
    cJSON *result = parse_llm_response(content->valuestring);
    cJSON_Delete(api_response);

    if (!result) {
        fprintf(stderr, "LLM: Failed to parse model output as valid quiz JSON\n");
    }

    return result;
}
