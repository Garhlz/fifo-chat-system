CC := gcc
CFLAGS := -Wall -Wextra -g -Iinclude -pthread
LDFLAGS := -pthread

BIN_DIR := bin
COMMON := src/chat.c src/config.c src/fifo.c src/log.c
SERVER_SRC := src/server.c src/daemon.c src/thread_pool.c src/handlers.c src/user_store.c $(COMMON)
CLIENT_SRC := src/client.c $(COMMON)
BOT_SRC := src/bot_manager.c $(COMMON)

TEST_BIN := $(BIN_DIR)/test_user_store
TEST_UNIT_SRC := test/test_user_store.c src/user_store.c src/chat.c

.PHONY: all clean test test_unit test_integration

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

$(TEST_BIN): $(TEST_UNIT_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_UNIT_SRC) $(LDFLAGS)

test_unit: $(TEST_BIN)
	@echo "=== 运行 C 单元测试 ==="
	./$(TEST_BIN)

test_integration: all
	@./test/test_flow.sh

test: test_unit test_integration
