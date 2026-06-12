#include "user_store.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures = 0;
static int test_num = 0;

#define TEST(name) do { test_num++; printf("  %-55s", name); } while(0)
#define OK()       printf("\033[32mOK\033[0m\n")
#define FAIL(msg)  do { failures++; printf("\033[31mFAIL\033[0m: %s\n", msg); } while(0)
#define CHECK(cond, msg) do { if (cond) OK(); else FAIL(msg); } while(0)

static void test_register_basic(void) {
    TEST("register new user");
    char err[128] = "";
    int rc = user_register("alice", "pass1", 0, err, sizeof(err));
    CHECK(rc == 0, err[0] ? err : "should succeed");

    TEST("register duplicate username");
    rc = user_register("alice", "pass2", 0, err, sizeof(err));
    CHECK(rc != 0, "should reject duplicate");

    TEST("register empty username");
    rc = user_register("", "pass", 0, err, sizeof(err));
    CHECK(rc != 0, "should reject empty username");

    TEST("register empty password");
    rc = user_register("bob_tmp", "", 0, err, sizeof(err));
    CHECK(rc != 0, "should reject empty password");
}

static void test_register_bot(void) {
    TEST("register bot user");
    char err[128] = "";
    int rc = user_register("bot_01", "bp", 1, err, sizeof(err));
    CHECK(rc == 0 && user_is_bot("bot_01"), "bot flag should be set");

    TEST("register second normal user");
    rc = user_register("bob", "passbob", 0, err, sizeof(err));
    CHECK(rc == 0, err[0] ? err : "should succeed");
}

static void test_password_check(void) {
    TEST("check correct password");
    int is_bot = -1;
    int rc = user_check_password("alice", "pass1", &is_bot);
    CHECK(rc == 0 && is_bot == 0, "should match and not be bot");

    TEST("check wrong password");
    rc = user_check_password("alice", "wrong", NULL);
    CHECK(rc != 0, "should reject wrong password");

    TEST("check nonexistent user");
    rc = user_check_password("nobody", "x", NULL);
    CHECK(rc != 0, "should reject nonexistent user");

    TEST("check bot password");
    rc = user_check_password("bot_01", "bp", &is_bot);
    CHECK(rc == 0 && is_bot == 1, "bot should be detectable");
}

static void test_online_status(void) {
    TEST("set user online");
    int rc = user_set_online("alice", "/tmp/fifo_alice");
    CHECK(rc == 0 && user_is_online("alice"), "should be online");

    TEST("user exists check");
    CHECK(user_exists("alice") && !user_exists("nobody"), "exists check");

    TEST("get online user fifo path");
    char path[256] = "";
    rc = user_get_fifo("alice", path, sizeof(path));
    CHECK(rc == 0 && strcmp(path, "/tmp/fifo_alice") == 0, "fifo path mismatch");

    TEST("set user offline");
    user_set_offline("alice");
    CHECK(!user_is_online("alice"), "should be offline");

    TEST("fifo unavailable after offline");
    rc = user_get_fifo("alice", path, sizeof(path));
    CHECK(rc != 0, "offline user should have no fifo");
}

static void test_online_list(void) {
    user_set_online("alice", "/tmp/fifo_alice");
    user_set_online("bob", "/tmp/fifo_bob");

    TEST("online list contains users");
    char buf[512] = "";
    int count = user_online_list(buf, sizeof(buf));
    CHECK(count >= 2 && strstr(buf, "alice") && strstr(buf, "bob"),
          "should list alice and bob");

    TEST("online list safe small buffer");
    char small[64] = "";
    user_online_list(small, sizeof(small));
    CHECK(1, "no crash even with small buffer"); /* just must not crash */
}

static void test_offline_messages(void) {
    user_set_offline("bob");
    time_t now = time(NULL);

    TEST("save offline message");
    int rc = user_save_offline("alice", "bob", "Hello bob!", now);
    CHECK(rc == 0, "should save");

    TEST("peek pending message");
    chat_message_t msgs[8];
    int n = user_peek_pending("bob", msgs, 8);
    CHECK(n == 1
          && strcmp(msgs[0].sender, "alice") == 0
          && strcmp(msgs[0].message, "Hello bob!") == 0,
          "pending message mismatch");

    TEST("mark pending as sent");
    user_mark_pending_sent("bob");
    n = user_peek_pending("bob", msgs, 8);
    CHECK(n == 0, "should be 0 after mark");
}

static void test_online_peers(void) {
    user_set_online("alice", "/tmp/fifo_alice");
    user_set_online("bob", "/tmp/fifo_bob");
    user_set_online("bot_01", "/tmp/fifo_bot1");

    TEST("online peers excluding alice");
    online_peer_t peers[16];
    int n = user_get_online_peers(peers, 16, "alice");
    int has_bob = 0, no_alice = 1;
    for (int i = 0; i < n; i++) {
        if (strcmp(peers[i].username, "bob") == 0) has_bob = 1;
        if (strcmp(peers[i].username, "alice") == 0) no_alice = 0;
    }
    CHECK(has_bob && no_alice, "should include bob, exclude alice");

    TEST("get online bots");
    char bots[4][CHAT_MAX_USERNAME];
    n = user_get_online_bots(bots, 4);
    CHECK(n >= 1 && strcmp(bots[0], "bot_01") == 0, "bot not in list");
}

int main(void) {
    printf("\n=== user_store 单元测试 ===\n\n");
    user_store_init();

    test_register_basic();
    test_register_bot();
    test_password_check();
    test_online_status();
    test_online_list();
    test_offline_messages();
    test_online_peers();

    printf("\n共 %d 项测试, ", test_num);
    if (failures) printf("\033[31m%d 项失败\033[0m\n", failures);
    else          printf("\033[32m全部通过\033[0m\n");
    return failures ? 1 : 0;
}
