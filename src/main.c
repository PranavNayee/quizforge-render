/*
 * main.c — HTTP server setup, routing, static file serving,
 *           /api/analyze endpoint, and /api/health endpoint.
 *
 * Uses the Mongoose embedded HTTP server for request handling
 * and cJSON for JSON parsing/generation.
 */
#include "text_clean.h"
#include "tfidf.h"
#include "chunker.h"
#include "keyterms.h"
#include "llm.h"
#include "fallback.h"
#include "util.h"

#include "mongoose.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <curl/curl.h>

/* Maximum request body size: 100 KB. */
#define MAX_BODY_SIZE (100 * 1024)

/* Default port. */
#define DEFAULT_PORT "8080"

static int s_signo = 0;

/* Signal handler for graceful shutdown. */
static void signal_handler(int signo) {
    s_signo = signo;
}

/* ── Security and CORS headers ─────────────────────────────────────── */

/* Add common security and CORS headers to a response. */
static void add_common_headers(struct mg_connection *c) {
    (void)c; /* Headers are added inline in mg_http_reply */
}

static const char *CORS_HEADERS =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "X-Frame-Options: DENY\r\n"
    "X-XSS-Protection: 1; mode=block\r\n"
    "Referrer-Policy: strict-origin-when-cross-origin\r\n";

/* ── JSON response helpers ─────────────────────────────────────────── */

/* Map an HTTP status code to its reason phrase. */
static const char *status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        default:  return "Internal Server Error";
    }
}

/* Send a JSON body with an exact Content-Length and a JSON Content-Type.
 * (mg_http_reply pads Content-Length with spaces, which strict HTTP
 * clients can reject, so we write the header ourselves.) */
static void send_json(struct mg_connection *c, int status, const char *body) {
    size_t len = body ? strlen(body) : 0;
    mg_printf(c,
              "HTTP/1.1 %d %s\r\n%s"
              "Content-Type: application/json; charset=utf-8\r\n"
              "Content-Length: %lu\r\n\r\n",
              status, status_text(status), CORS_HEADERS, (unsigned long)len);
    if (len > 0) mg_send(c, body, len);
}

static void send_json_error(struct mg_connection *c, int status,
                             const char *message) {
    char *escaped = json_escape(message);
    size_t n = strlen(escaped) + 32;
    char *body = (char *)malloc(n);
    if (body) {
        snprintf(body, n, "{\"error\":\"%s\"}", escaped);
        send_json(c, status, body);
        free(body);
    } else {
        send_json(c, 500, "{\"error\":\"Out of memory\"}");
    }
    free(escaped);
}

/* ── Health endpoint ───────────────────────────────────────────────── */

static void handle_health(struct mg_connection *c) {
    send_json(c, 200, "{\"status\":\"ok\"}");
}

/* ── Analyze endpoint ──────────────────────────────────────────────── */

static void handle_analyze(struct mg_connection *c, struct mg_http_message *hm) {
    uint64_t start_ms = time_ms();

    /* Check content length */
    if (hm->body.len > MAX_BODY_SIZE) {
        send_json_error(c, 413, "Request body too large (max 100 KB)");
        return;
    }

    /* Parse request JSON */
    char *body_str = (char *)malloc(hm->body.len + 1);
    if (!body_str) {
        send_json_error(c, 500, "Memory allocation failed");
        return;
    }
    memcpy(body_str, hm->body.buf, hm->body.len);
    body_str[hm->body.len] = '\0';

    cJSON *req = cJSON_Parse(body_str);
    free(body_str);

    if (!req) {
        send_json_error(c, 400, "Invalid JSON in request body");
        return;
    }

    /* Extract fields */
    const cJSON *text_field = cJSON_GetObjectItem(req, "text");
    if (!cJSON_IsString(text_field) || !text_field->valuestring[0]) {
        cJSON_Delete(req);
        send_json_error(c, 400, "Missing or empty 'text' field");
        return;
    }

    const char *text = text_field->valuestring;
    size_t text_len = strlen(text);

    int num_questions = 8;
    const cJSON *nq = cJSON_GetObjectItem(req, "num_questions");
    if (cJSON_IsNumber(nq)) {
        num_questions = nq->valueint;
        if (num_questions < 1) num_questions = 1;
        if (num_questions > 20) num_questions = 20;
    }

    const char *difficulty = "medium";
    const cJSON *diff = cJSON_GetObjectItem(req, "difficulty");
    if (cJSON_IsString(diff) && diff->valuestring[0]) {
        if (strcmp(diff->valuestring, "easy") == 0 ||
            strcmp(diff->valuestring, "medium") == 0 ||
            strcmp(diff->valuestring, "hard") == 0) {
            difficulty = diff->valuestring;
        }
    }

    const char *mode = "both";
    const cJSON *mode_field = cJSON_GetObjectItem(req, "mode");
    if (cJSON_IsString(mode_field) && mode_field->valuestring[0]) {
        if (strcmp(mode_field->valuestring, "mcq") == 0 ||
            strcmp(mode_field->valuestring, "flashcards") == 0 ||
            strcmp(mode_field->valuestring, "both") == 0) {
            mode = mode_field->valuestring;
        }
    }

    /* Step 1 & 2: Clean text and split into sentences */
    CleanResult clean = text_clean_and_split(text, text_len);
    if (clean.count == 0) {
        cJSON_Delete(req);
        send_json_error(c, 400,
            "No valid sentences found. Please provide more text (at least a few sentences with 5+ words each).");
        clean_result_free(&clean);
        return;
    }

    /* Step 3 & 4: TF-IDF scoring */
    TFIDFResult tfidf = tfidf_score(clean.sentences, clean.count);

    /* Step 5: Chunking */
    /* Keep roughly the top 60% of sentences (min 3, max 12) so the AI
     * receives focused input instead of the whole text. */
    int top_k = (clean.count * 6 + 9) / 10;
    if (top_k > 12) top_k = 12;
    if (top_k < 3) top_k = 3;
    if (top_k > clean.count) top_k = clean.count;
    ChunkResult chunks = chunk_sentences(clean.sentences, clean.count,
                                          &tfidf, top_k);

    /* Step 6: Key term extraction */
    KeyTermResult key_terms = extract_key_terms(clean.sentences, clean.count,
                                                 &tfidf, 12);

    /* Step 7: Try LLM generation, fall back to offline */
    const char *source = "offline";
    cJSON *quiz_data = NULL;

    if (llm_available()) {
        quiz_data = llm_generate(&chunks, &key_terms, num_questions,
                                  difficulty, mode);
        if (quiz_data) {
            source = "ai";
        }
    }

    if (!quiz_data) {
        quiz_data = fallback_generate(clean.sentences, clean.count,
                                       &tfidf, &chunks, &key_terms,
                                       num_questions, mode);
        source = "offline";
    }

    uint64_t elapsed = time_ms() - start_ms;

    /* Build the response JSON */
    cJSON *response = cJSON_CreateObject();

    /* Stats */
    cJSON *stats = cJSON_CreateObject();
    cJSON_AddNumberToObject(stats, "words", clean.total_words);
    cJSON_AddNumberToObject(stats, "sentences", clean.count);
    cJSON_AddNumberToObject(stats, "chunks", chunks.count);

    /* Reduction percent: how much text was reduced by chunking */
    double reduction = 0.0;
    if (clean.count > 0 && chunks.count > 0) {
        int chunk_sents = 0;
        for (int i = 0; i < chunks.count; i++) {
            chunk_sents += chunks.chunks[i].sentence_count;
        }
        reduction = (1.0 - (double)chunk_sents / (double)clean.count) * 100.0;
        if (reduction < 0.0) reduction = 0.0;
    }
    cJSON_AddNumberToObject(stats, "reduction_percent", (int)reduction);
    cJSON_AddNumberToObject(stats, "processing_ms", (double)elapsed);
    cJSON_AddItemToObject(response, "stats", stats);

    /* Key terms */
    cJSON *kt_arr = cJSON_CreateArray();
    for (int i = 0; i < key_terms.count; i++) {
        cJSON *kt = cJSON_CreateObject();
        cJSON_AddStringToObject(kt, "term", key_terms.terms[i].term);
        cJSON_AddNumberToObject(kt, "weight", key_terms.terms[i].weight);
        cJSON_AddItemToArray(kt_arr, kt);
    }
    cJSON_AddItemToObject(response, "key_terms", kt_arr);

    /* Sentences with importance scores (for heatmap) */
    cJSON *sent_arr = cJSON_CreateArray();
    for (int i = 0; i < clean.count; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "text", clean.sentences[i].text);
        double score = (i < tfidf.count) ? tfidf.scored[i].norm_score : 0.0;
        cJSON_AddNumberToObject(s, "score", score);
        cJSON_AddItemToArray(sent_arr, s);
    }
    cJSON_AddItemToObject(response, "sentences", sent_arr);

    /* Chunks */
    cJSON *chunk_arr = cJSON_CreateArray();
    for (int i = 0; i < chunks.count; i++) {
        cJSON *ch = cJSON_CreateObject();
        cJSON_AddNumberToObject(ch, "id", chunks.chunks[i].id);
        cJSON_AddStringToObject(ch, "text", chunks.chunks[i].text);
        cJSON_AddNumberToObject(ch, "score", chunks.chunks[i].score);
        cJSON *kw_arr = cJSON_CreateArray();
        for (int k = 0; k < chunks.chunks[i].keyword_count; k++) {
            cJSON_AddItemToArray(kw_arr,
                cJSON_CreateString(chunks.chunks[i].keywords[k]));
        }
        cJSON_AddItemToObject(ch, "keywords", kw_arr);
        cJSON_AddItemToArray(chunk_arr, ch);
    }
    cJSON_AddItemToObject(response, "chunks", chunk_arr);

    /* MCQs and flashcards from quiz_data */
    if (quiz_data) {
        cJSON *mcqs = cJSON_GetObjectItem(quiz_data, "mcqs");
        if (mcqs) {
            cJSON_AddItemToObject(response, "mcqs",
                                  cJSON_Duplicate(mcqs, 1));
        } else {
            cJSON_AddItemToObject(response, "mcqs", cJSON_CreateArray());
        }

        cJSON *flashcards = cJSON_GetObjectItem(quiz_data, "flashcards");
        if (flashcards) {
            cJSON_AddItemToObject(response, "flashcards",
                                  cJSON_Duplicate(flashcards, 1));
        } else {
            cJSON_AddItemToObject(response, "flashcards", cJSON_CreateArray());
        }
        cJSON_Delete(quiz_data);
    } else {
        cJSON_AddItemToObject(response, "mcqs", cJSON_CreateArray());
        cJSON_AddItemToObject(response, "flashcards", cJSON_CreateArray());
    }

    cJSON_AddStringToObject(response, "source", source);

    /* Serialize and send */
    char *json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    cJSON_Delete(req);

    /* Cleanup */
    keyterm_result_free(&key_terms);
    chunk_result_free(&chunks);
    tfidf_result_free(&tfidf);
    clean_result_free(&clean);

    if (json_str) {
        send_json(c, 200, json_str);
        free(json_str);
    } else {
        send_json_error(c, 500, "Failed to serialize response");
    }
}

/* ── HTTP event handler ────────────────────────────────────────────── */

static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        /* CORS preflight */
        if (mg_match(hm->method, mg_str("OPTIONS"), NULL)) {
            send_json(c, 204, NULL);
            return;
        }

        /* API routes */
        if (mg_match(hm->uri, mg_str("/api/health"), NULL)) {
            handle_health(c);
            return;
        }

        if (mg_match(hm->uri, mg_str("/api/analyze"), NULL)) {
            if (mg_match(hm->method, mg_str("POST"), NULL)) {
                handle_analyze(c, hm);
            } else {
                send_json_error(c, 405, "Method not allowed. Use POST.");
            }
            return;
        }

        /* Static file serving from /web directory */
        struct mg_http_serve_opts opts = {
            .root_dir = "web",
            .ssi_pattern = NULL,
            .extra_headers = "X-Content-Type-Options: nosniff\r\n"
                             "X-Frame-Options: DENY\r\n"
        };
        mg_http_serve_dir(c, hm, &opts);
    }

    add_common_headers(c);
}

/* ── Main ──────────────────────────────────────────────────────────── */

int main(void) {
    /* Initialize libcurl globally (must be done once before any curl call) */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* Set up signal handling for graceful shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Determine the port */
    const char *port_str = getenv("PORT");
    if (!port_str || port_str[0] == '\0') {
        port_str = DEFAULT_PORT;
    }

    char listen_url[128];
    snprintf(listen_url, sizeof(listen_url), "http://0.0.0.0:%s", port_str);

    /* Initialize Mongoose */
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    struct mg_connection *conn = mg_http_listen(&mgr, listen_url, ev_handler, NULL);
    if (!conn) {
        fprintf(stderr, "Failed to bind to %s\n", listen_url);
        mg_mgr_free(&mgr);
        curl_global_cleanup();
        return 1;
    }

    printf("QuizForge server running at %s\n", listen_url);
    printf("LLM API: %s\n", llm_available() ? "configured" : "not configured (offline mode)");

    /* Event loop */
    while (s_signo == 0) {
        mg_mgr_poll(&mgr, 200);
    }

    printf("\nShutting down...\n");
    mg_mgr_free(&mgr);
    curl_global_cleanup();

    return 0;
}
