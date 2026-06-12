CC := gcc
CFLAGS := -Wall -Wextra -g -Iinclude -pthread
LDFLAGS := -pthread

BIN_DIR := bin
COMMON := src/chat.c src/config.c src/fifo.c src/log.c
SERVER_SRC := src/server.c src/daemon.c src/thread_pool.c src/handlers.c src/user_store.c $(COMMON)
CLIENT_SRC := src/client.c $(COMMON)
BOT_SRC := src/bot_manager.c $(COMMON)

.PHONY: all clean

all: $(BIN_DIR)/chatserver $(BIN_DIR)/client $(BIN_DIR)/bot_manager

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/chatserver: $(SERVER_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)

$(BIN_DIR)/client: $(CLIENT_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC) $(LDFLAGS)

$(BIN_DIR)/bot_manager: $(BOT_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(BOT_SRC) $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)
