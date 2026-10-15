CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -Isrc -Ivendor -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -DMG_ENABLE_LOG=0
LDFLAGS = -lcurl -lm -lpthread

SRC     = src/main.c src/util.c src/text_clean.c src/tfidf.c \
          src/chunker.c src/keyterms.c src/llm.c src/fallback.c \
          vendor/mongoose.c vendor/cJSON.c

OBJ     = $(SRC:.c=.o)
BIN     = quizforge

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(OBJ) $(BIN)
