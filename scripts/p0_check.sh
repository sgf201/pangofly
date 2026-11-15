#!/bin/sh
# 崩溃恢复 + 类型校验的端到端验证
#
# 前置：先构建出 shm_p0_check（与 shm_example 同目录）
#   cmake -S <pangofly> -B <build> && cmake --build <build>
#
# 用法：
#   ./p0_check.sh <shm_p0_check 可执行文件路径>
#
# 退出码 0 表示全部通过。可在 x86 或板子上直接跑。

BIN=${1:-}
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
  echo "usage: $0 <path/to/shm_p0_check>" >&2
  exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*"; exit 1; }

cleanup_channel() {
  rm -f "/dev/shm/pangofly_v2_$1" 2>/dev/null
  rm -f "/dev/shm/pangofly_v2_registry" 2>/dev/null
}

# 等持有者打印 HOLD_READY，最多 5 秒
wait_ready() {
  i=0
  while [ $i -lt 50 ]; do
    grep -q HOLD_READY "$1" 2>/dev/null && return 0
    sleep 0.1
    i=$((i + 1))
  done
  return 1
}

echo "############ 1. 死持有者的锁可被接管 ############"
OUT=$("$BIN" locktakeover 2>&1)
echo "$OUT"
echo "$OUT" | grep -q "held_by_me=1" || fail "锁接管失败"
US=$(echo "$OUT" | sed -n 's/.*elapsed_us=\([0-9]*\).*/\1/p')
[ -n "$US" ] || fail "未取得接管耗时"
[ "$US" -lt 200000 ] || fail "接管耗时过长: ${US}us"
echo "  -> 接管耗时 ${US}us"

echo
echo "############ 2. 类型不匹配必须被拒绝 ############"
cleanup_channel typecheck_ch
OUT=$("$BIN" type_a typecheck_ch 2>&1)
echo "$OUT"
echo "$OUT" | grep -q "TYPE_A writer=ok" || fail "类型 A 建 writer 失败"

OUT=$("$BIN" type_b typecheck_ch 2>&1)
echo "$OUT"
echo "$OUT" | grep -q "TYPE_B reader=null" || fail "类型 B 未被拒绝"
echo "$OUT" | grep -q "TYPE MISMATCH" || fail "缺少 TYPE MISMATCH 报错"
echo "  -> 跨进程类型不匹配已拦截"

cleanup_channel typecheck_inproc
OUT=$("$BIN" type_inproc typecheck_inproc 2>&1)
echo "$OUT"
echo "$OUT" | grep -q "second=null" || fail "进程内复用段未被类型校验拦住"
echo "  -> 进程内缓存复用同样校验类型"
cleanup_channel typecheck_ch
cleanup_channel typecheck_inproc

echo
echo "############ 3. 写端 kill -9 后块可被回收 ############"
cleanup_channel crash_ch
"$BIN" hold crash_ch 1 > "$TMP/hold.log" 2>&1 &
HOLD_PID=$!
wait_ready "$TMP/hold.log" || fail "持有者未进入 WRITING"
cat "$TMP/hold.log"
kill -9 "$HOLD_PID" 2>/dev/null
wait "$HOLD_PID" 2>/dev/null
sleep 0.2
[ -d "/proc/$HOLD_PID" ] && fail "持有者进程仍存在"

echo "  已 kill -9；用同 channel 连续写 6 帧（块数 3）"
OUT=$("$BIN" write crash_ch 2>&1)
echo "$OUT"
echo "$OUT" | grep -q "WRITE_OK frames=6" || fail "崩溃后仍无法继续写"
echo "$OUT" | grep -q "reclaim zombie block" || fail "没有触发僵尸块回收"
echo "  -> 崩溃留下的 WRITING 块已被回收并复用"
cleanup_channel crash_ch

echo
echo "############ 4. 健康持有者不得被误抢 ############"
cleanup_channel healthy_ch
# 占满全部 3 个块并停在 WRITING：写端此时必须背压，且不得回收这些块
"$BIN" hold healthy_ch 3 > "$TMP/hold2.log" 2>&1 &
HOLD2=$!
wait_ready "$TMP/hold2.log" || fail "第二个持有者未就绪"
cat "$TMP/hold2.log"

OUT=$("$BIN" write healthy_ch 2>&1)
echo "$OUT"
echo "$OUT" | grep -q "WRITE_OK" && fail "健康持有者的块被误抢"
echo "$OUT" | grep -q "reclaim zombie" && fail "对健康持有者误报僵尸"
echo "$OUT" | grep -q "WRITE loan failed" || fail "预期背压，但没看到 loan failed"
echo "  -> 持有者存活时正确背压，未误抢"

kill -9 "$HOLD2" 2>/dev/null
wait "$HOLD2" 2>/dev/null
sleep 0.2
OUT=$("$BIN" write healthy_ch 2>&1)
echo "$OUT"
echo "$OUT" | grep -q "WRITE_OK" || fail "持有者死亡后仍无法恢复写入"
echo "$OUT" | grep -q "reclaim zombie" || fail "未回收 3 个僵尸块"
echo "  -> 持有者退出后 3 个块全部回收，写入恢复"
cleanup_channel healthy_ch

echo
echo "############ 5. 读端必须被写端唤醒（futex，而非固定轮询） ############"
cleanup_channel wake_ch
# 读端先进 Wait(2s)；写端 300ms 后才发布。若唤醒失效，读端会一直睡到超时。
"$BIN" waitwake wake_ch > "$TMP/wait.log" 2>&1 &
WAIT_PID=$!
sleep 0.1
"$BIN" wakesend wake_ch > "$TMP/send.log" 2>&1 || fail "发送端失败"
wait "$WAIT_PID" || fail "读端未成功取到消息"
cat "$TMP/wait.log"
cat "$TMP/send.log"
grep -q "WAIT_WAKE" "$TMP/wait.log" || fail "读端未收到消息"
# 发送端写入 payload=2048 字节，读端必须看到同样的长度
grep -q "payload=2048" "$TMP/wait.log" || fail "读到的消息内容不对"
US=$(sed -n 's/.*elapsed_us=\([0-9]*\).*/\1/p' "$TMP/wait.log" | head -1)
# 写端在 300ms 后发布，读端总耗时应略大于 300ms 且远小于 2s 超时
[ -n "$US" ] || fail "未取得等待耗时"
[ "$US" -gt 250000 ] || fail "读端过早返回，唤醒逻辑可疑: ${US}us"
[ "$US" -lt 1500000 ] || fail "读端等待过久，疑似未被唤醒: ${US}us"
echo "  -> 读端在写端发布后立即被唤醒（${US}us ≈ 300ms 发送延时）"
cleanup_channel wake_ch

echo
echo "############ 6. 跨进程窗口分配不得重叠（flock 互斥） ############"
# 多个进程同时申请窗口。无互斥时槽位分配的多步写会交错，
# 可能让两个不同 channel 拿到同一段 VA。
for i in 1 2 3 4 5 6; do
  "$BIN" slot "slot_tag_$i" > "$TMP/slot_$i.log" 2>&1 &
done
wait
SLOT_OK=0
ADDRS=""
for i in 1 2 3 4 5 6; do
  cat "$TMP/slot_$i.log"
  if grep -q "SLOT_OK" "$TMP/slot_$i.log"; then
    SLOT_OK=$((SLOT_OK + 1))
    A=$(sed -n 's/.*addr=\(0x[0-9a-f]*\).*/\1/p' "$TMP/slot_$i.log" | head -1)
    ADDRS="$ADDRS $A"
  fi
done
[ "$SLOT_OK" -ge 6 ] || fail "并发申请窗口失败：仅 $SLOT_OK/6 成功"
UNIQ=$(echo "$ADDRS" | tr ' ' '\n' | grep -v '^$' | sort -u | wc -l)
[ "$UNIQ" -eq 6 ] || fail "出现地址重叠：6 个 channel 只有 $UNIQ 个不同地址"
echo "  -> 6 个进程并发申请到 6 个互不重叠的窗口"
# 清理本轮占用的注册表槽位：删掉注册表，下次 Init 会重建
rm -f /dev/shm/pangofly_v2_registry 2>/dev/null || true

echo
echo "############ 7. 同一 Node 内同 channel 的 Reader 去重 ############"
cleanup_channel dup_ch
OUT=$("$BIN" dupreader dup_ch 2>&1)
echo "$OUT"
echo "$OUT" | grep -q "first=ok" || fail "第一个 Reader 创建失败"
echo "$OUT" | grep -q "second=null" || fail "第二个 Reader 未被拒绝"
echo "$OUT" | grep -q "already exists" || fail "缺少重复 Reader 的报错"
echo "$OUT" | grep -q "after_release=ok" || fail "释放后无法重建 Reader"
echo "  -> 重复 Reader 被拒绝，释放后可重建"
cleanup_channel dup_ch

echo
echo "ALL P0 TESTS PASSED"
