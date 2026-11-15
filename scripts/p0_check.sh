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
echo "ALL P0 TESTS PASSED"
