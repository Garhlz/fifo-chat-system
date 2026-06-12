#!/bin/bash
# 集成测试 — 覆盖完整用户和机器人流程
# 需先 make，从项目根目录运行:  ./test/test_flow.sh
set -u

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ"
CONF="$PROJ/config/server.conf"
export CHAT_CONFIG="$CONF"

SERVER="./bin/chatserver"
CLIENT="./bin/client"
BOTMGR="./bin/bot_manager"
FIFO_DIR="$HOME/Server/fifo"
LOG_DIR="$HOME/log/chat-logs/server"
SERVER_LOG="$LOG_DIR/server.log"
THREAD_LOG="$LOG_DIR/threads.log"

PASS=0; FAIL=0
GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }

check_log() {
    local label="$1"; shift
    if grep -q "$@" "$SERVER_LOG" 2>/dev/null; then
        pass "$label"
    else
        fail "$label (log 中未找到: $*)"
    fi
}

cleanup() {
    if [ -n "${SRV_PID:-}" ] && kill -0 "$SRV_PID" 2>/dev/null; then
        kill "$SRV_PID" 2>/dev/null || true
        sleep 0.5
    fi
    if [ -n "${BOTMGR_PID:-}" ] && kill -0 "$BOTMGR_PID" 2>/dev/null; then
        kill "$BOTMGR_PID" 2>/dev/null || true
    fi
    rm -rf "$FIFO_DIR" 2>/dev/null || true
    rm -rf "$LOG_DIR" 2>/dev/null || true
    rm -f /tmp/fifo_chat_bots.txt
    rm -f /tmp/test_*.txt /tmp/test_*_in
}

# ── 预检 ──
echo "=== FIFO 聊天系统集成测试 ==="
echo ""

if [ ! -x "$SERVER" ] || [ ! -x "$CLIENT" ] || [ ! -x "$BOTMGR" ]; then
    echo "请先运行 make 编译所有程序"
    exit 1
fi

# ── 1. 启动服务器 ──
echo "[1/7] 启动服务器"
cleanup
"$SERVER" --foreground "$CONF" > /dev/null 2>&1 &
SRV_PID=$!
sleep 1

if ! kill -0 "$SRV_PID" 2>/dev/null; then
    fail "服务器启动失败"
    exit 1
fi
pass "服务器已启动 (pid=$SRV_PID)"

# ── 2. 公共 FIFO ──
echo "[2/7] 公共 FIFO"
for f in ynp_reg_fifo ynp_login_fifo ynp_msg_fifo ynp_logout_fifo; do
    if [ -p "$FIFO_DIR/$f" ]; then
        pass "FIFO 存在: $f"
    else
        fail "FIFO 缺失: $f"
    fi
done

# ── 3. 注册 ──
echo "[3/7] 用户注册"
U1="szu_user_1"
U2="szu_user_2"
PW="test123"

"$CLIENT" register "$U1" "$PW" > /tmp/test_reg1.txt 2>&1 && pass "注册: $U1" || fail "注册: $U1"
"$CLIENT" register "$U2" "$PW" > /tmp/test_reg2.txt 2>&1 && pass "注册: $U2" || fail "注册: $U2"
"$CLIENT" register "$U2" "$PW" > /tmp/test_reg3.txt 2>&1 && fail "重复注册应被拒: $U2" || pass "重复注册被拒: $U2"

check_log "注册事件已记录" "register_success"

# ── 4. 登录 ──
echo "[4/7] 用户登录"

# 使用 FIFO 控制两个客户端的 stdin
U1_IN="/tmp/test_u1_in"
U2_IN="/tmp/test_u2_in"
mkfifo "$U1_IN" "$U2_IN" 2>/dev/null || true

# exec <> 打开 FIFO 不会阻塞（O_RDWR），且保持 writer 存在
exec 19<>"$U1_IN"
exec 20<>"$U2_IN"

# 启动用户2（接收消息方），后台运行
"$CLIENT" login "$U2" "$PW" < "$U2_IN" > /tmp/test_u2_out.txt 2>&1 &
U2_PID=$!
sleep 0.5

# 启动用户1（发送消息方）
"$CLIENT" login "$U1" "$PW" < "$U1_IN" > /tmp/test_u1_out.txt 2>&1 &
U1_PID=$!
sleep 0.8

check_log "登录事件已记录" "login_success"
check_log "线程有分派记录" "dispatched" "$THREAD_LOG"
check_log "线程有回收记录" "recycled" "$THREAD_LOG"

# ── 5. 在线消息 ──
echo "[5/7] 在线消息（用户1 → 用户2 连续5条）"

for i in 1 2 3 4 5; do
    echo "send $U2 Hello-$i" >&19
    sleep 0.2
done
echo "logout" >&19
wait $U1_PID 2>/dev/null || true
sleep 0.3

# 关闭用户2 的输入通道
echo "logout" >&20
sleep 0.3
exec 20>&-
wait $U2_PID 2>/dev/null || true

# 关闭 fd
exec 19>&- 2>/dev/null || true
rm -f "$U1_IN" "$U2_IN"

check_log "在线消息已发送" "msg_sent.*$U1.*$U2"
MSG_COUNT=$(grep -c "msg_sent.*$U1.*$U2" "$SERVER_LOG" 2>/dev/null || echo 0)
if [ "$MSG_COUNT" -ge 5 ]; then
    pass "连续5条在线消息已记录 ($MSG_COUNT 条)"
else
    fail "在线消息不足5条 (仅 $MSG_COUNT 条)"
fi

# ── 6. 离线消息 ──
echo "[6/7] 离线消息"

# 用户2 已退出，用户1 发一条离线消息
U1_IN2="/tmp/test_u1_in2"
mkfifo "$U1_IN2" 2>/dev/null || true
exec 21<>"$U1_IN2"
"$CLIENT" login "$U1" "$PW" < "$U1_IN2" > /tmp/test_off1.txt 2>&1 &
U1B_PID=$!
sleep 0.5
echo "send $U2 Offline-msg-你好" >&21
echo "logout" >&21
wait $U1B_PID 2>/dev/null || true
exec 21>&- 2>/dev/null || true
rm -f "$U1_IN2"

check_log "离线消息已保存 (pending)" "offline_pending.*$U1.*$U2"

# 用户2 重新登录，应收到离线消息
U2_IN2="/tmp/test_u2_in2"
mkfifo "$U2_IN2" 2>/dev/null || true
exec 22<>"$U2_IN2"
"$CLIENT" login "$U2" "$PW" < "$U2_IN2" > /tmp/test_off2.txt 2>&1 &
U2B_PID=$!
sleep 0.8
echo "logout" >&22
wait $U2B_PID 2>/dev/null || true
exec 22>&- 2>/dev/null || true
rm -f "$U2_IN2"

check_log "离线消息已推送 (sent)" "offline_sent.*$U1.*$U2"

# ── 7. 机器人 ──
echo "[7/7] 机器人管理"

# 增加3个机器人（后台运行，保持在线以接收消息）
"$BOTMGR" add 3 > /tmp/test_bot_add.txt 2>&1 &
BOTMGR_PID=$!
sleep 1.5

check_log "机器人注册记录" "bot_register_success"
check_log "机器人登录记录" "bot_login_success"

# 提取机器人名，在 bot_manager 还运行时发消息
BOT_NAME=$(grep -oP 'bot_ynp_\d+_\d+' "$SERVER_LOG" 2>/dev/null | head -1)
if [ -n "$BOT_NAME" ]; then
    U1_IN3="/tmp/test_u1_in3"
    mkfifo "$U1_IN3" 2>/dev/null || true
    exec 23<>"$U1_IN3"
    "$CLIENT" login "$U1" "$PW" < "$U1_IN3" > /tmp/test_bot_msg.txt 2>&1 &
    U1C_PID=$!
    sleep 0.5
    echo "send $BOT_NAME 你好机器人" >&23
    sleep 0.5
    echo "logout" >&23
    wait $U1C_PID 2>/dev/null || true
    exec 23>&- 2>/dev/null || true
    rm -f "$U1_IN3"

    check_log "用户与机器人消息已记录" "msg_sent.*$U1.*$BOT_NAME"
else
    fail "无法提取机器人用户名"
fi

# 停止 bot_manager 进程
kill $BOTMGR_PID 2>/dev/null || true
sleep 0.3

# 减少2个机器人
timeout 3 "$BOTMGR" del 2 > /tmp/test_bot_del.txt 2>&1 || true
sleep 0.5
check_log "机器人退出记录" "bot_logout"

# ── 收尾 ──
echo ""
echo "--- server.log 最后20行 ---"
tail -20 "$SERVER_LOG" 2>/dev/null || echo "(日志为空)"
echo ""

cleanup

echo "=============================="
TOTAL=$((PASS + FAIL))
echo "总计: $TOTAL  通过: ${GREEN}$PASS${NC}"
if [ "$FAIL" -gt 0 ]; then
    echo "失败: ${RED}$FAIL${NC}"
    exit 1
else
    echo -e "${GREEN}全部通过!${NC}"
    exit 0
fi
