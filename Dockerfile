# ---------- Stage 1: build the C binary ----------
FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends gcc make libc6-dev libcurl4-openssl-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY Makefile ./
COPY src ./src
COPY vendor ./vendor
RUN make

# ---------- Stage 2: slim runtime ----------
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends libcurl4 ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --create-home --uid 10001 quizforge
WORKDIR /app
COPY --from=build /src/quizforge ./quizforge
COPY web ./web
RUN chown -R quizforge:quizforge /app
USER quizforge

# Render injects $PORT at runtime; 8080 is the local default.
ENV PORT=8080
EXPOSE 8080
CMD ["./quizforge"]
