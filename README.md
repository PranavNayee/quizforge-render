# ⚡ QuizForge — AI Study Notes to Quiz Generator

Paste your study notes and get multiple-choice questions and flashcards.

The interesting part: **a hand-written C engine decides what is important**
(TF-IDF sentence ranking, key-term extraction, chunking). The LLM only sees
those focused chunks, never the whole text. No API key? It still works,
using an offline cloze-question generator.

**Stack:** C11 · Mongoose (HTTP) · cJSON · libcurl · HTML/CSS/vanilla JS · Docker · Render

---

## Architecture

```
 Browser (HTML/CSS/JS)
        │  POST /api/analyze  {text, num_questions, difficulty, mode}
        ▼
 ┌──────────────────────── C backend ────────────────────────┐
 │ 1 clean text ─► 2 split sentences ─► 3 tokenize + stem     │
 │        ─► 4 TF-IDF score ─► 5 pick top sentences + chunk   │
 │        ─► 6 key terms ─► 7 importance map (heatmap)        │
 │                        │                                   │
 │          LLM key set?──┤                                   │
 │            yes ▼       ▼ no / any failure                  │
 │      Nemotron (libcurl)   Offline cloze generator          │
 └────────────────────────────┬───────────────────────────────┘
                              ▼
        JSON: stats, key_terms, sentences, chunks, mcqs, flashcards, source
```

## How the scoring works (plain English)

1. Every sentence is treated like a tiny document.
2. Words are lowercased, stopwords ("the", "is"…) are dropped, and a simple
   stemmer merges forms like *reaction / reactions*.
3. A word is valuable if it appears **often overall** but in **few
   sentences** (that makes it specific). Formula per word:
   `tf × (log((N+1)/(df+1)) + 1)`.
4. A sentence's score = sum of its word values ÷ √(word count), so long
   sentences don't win just for being long.
5. Bonuses/penalties: +15% definition patterns ("is defined as", "refers
   to"), +10% numbers/formulas, +10% near the start, +10% capitalized
   multi-word terms, −20% questions.
6. The top ~60% of sentences (max 12) are regrouped in original order into
   2–3 sentence chunks. Those chunks, plus the key terms, are what the LLM
   receives. The "% Reduced" card shows how much text was filtered out.

## Project layout

```
src/      main.c  text_clean  tfidf  chunker  keyterms  llm  fallback  util
vendor/   mongoose.c/.h  cJSON.c/.h   (single-file libraries, vendored)
web/      index.html  style.css  app.js
Makefile  Dockerfile  render.yaml  .env.example
```

## Run locally

**Requirements:** gcc, make, libcurl development headers.

```bash
# Debian/Ubuntu:  sudo apt install build-essential libcurl4-openssl-dev
# macOS:          xcode-select --install   (libcurl is included)
# Windows:        use WSL2, or use Docker below

make
./quizforge                 # open http://localhost:8080
```

With the AI (optional). The server reads real environment variables; it
does **not** parse `.env` files itself:

```bash
cp .env.example .env        # then put your key in .env
set -a; source .env; set +a
./quizforge
```

With Docker:

```bash
docker build -t quizforge .
docker run --rm -p 8080:8080 --env-file .env quizforge
```

Quick API test:

```bash
curl -s localhost:8080/api/health
curl -s -X POST localhost:8080/api/analyze \
  -H 'Content-Type: application/json' \
  -d '{"text":"Photosynthesis is the process by which plants convert light energy into chemical energy. It takes place in the chloroplasts, which contain chlorophyll. The Calvin cycle fixes carbon dioxide into glucose using the enzyme RuBisCO.","num_questions":4,"mode":"both"}'
```

## Environment variables

| Variable      | Required | Default                                              | Purpose                              |
|---------------|----------|------------------------------------------------------|--------------------------------------|
| `LLM_API_KEY` | No*      | —                                                    | NVIDIA API key. Without it, offline mode is used. |
| `LLM_API_URL` | No       | `https://integrate.api.nvidia.com/v1/chat/completions` | Any OpenAI-compatible endpoint       |
| `LLM_MODEL`   | No       | `nvidia/llama-3.1-nemotron-70b-instruct`             | Model name (check build.nvidia.com; names change) |
| `PORT`        | No       | `8080`                                               | Render sets this automatically       |

\*Required only for AI mode.

## Deploy: GitHub + Render

1. **Push to GitHub**
   ```bash
   git init
   git add .
   git commit -m "QuizForge"
   git branch -M main
   git remote add origin https://github.com/YOUR-USERNAME/quizforge.git
   git push -u origin main
   ```
   Check that `.env` is **not** in the commit (it is git-ignored).
2. On [render.com](https://render.com): **New → Blueprint** (uses `render.yaml`)
   or **New → Web Service**, pick your repo, choose **Docker** as runtime.
3. Add the environment variable `LLM_API_KEY` in the Render dashboard.
4. Deploy. Health check path is `/api/health`.
5. Replace `YOUR-USERNAME` in the footer link inside `web/index.html`.

> **Free tier note:** Render free web services sleep after ~15 minutes of
> inactivity, so the first request afterwards can take 30–60 seconds.

## Limits & safety

- Request body max 100 KB; clear JSON errors on bad input.
- LLM calls time out after 30 s, then the app falls back to offline mode.
- The server handles one request at a time, so a slow LLM call briefly delays
  other users. Fine for a demo; see ideas below for scaling.
- All text rendered in the UI uses `textContent` (no XSS via notes or AI output).
- Notes are processed in memory and never stored.

## Troubleshooting

| Problem | Fix |
|---|---|
| `curl/curl.h: No such file` | Install libcurl dev headers (see Run locally). |
| Badge says "Offline Mode" | `LLM_API_KEY` not set or the API call failed; check key/model name. |
| "No valid sentences found" | Use full sentences with 5+ words each. |
| Render first load is slow | Free tier cold start; wait up to a minute. |
| Port already in use | `PORT=9000 ./quizforge` |

## Ideas for next steps

- Handle requests in worker threads so LLM calls don't block others.
- Rate limiting per IP.
- PDF upload (extract text server-side).
- Spaced-repetition scheduling for flashcards.
- Save quizzes with shareable links.
