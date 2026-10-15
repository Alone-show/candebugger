#!/usr/bin/env bash
# CanDebugger L3 实机测试编排（需 USBCANFD-200U 设备与 USB 权限）
#
# 用法：
#   scripts/run_hw_tests.sh [build_dir]
# 说明：串行执行，每个用例独立进程，带超时与耗时统计。
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
TIMEOUT="${TIMEOUT:-180}"

red()   { printf '\033[31m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
info()  { printf '\033[36m%s\033[0m\n' "$*"; }

if [ ! -x "$BUILD/device_smoke" ]; then
    red "未找到实机测试二进制，请先构建：cmake --build $BUILD"
    exit 2
fi

if ! lsusb 2>/dev/null | grep -q '3068:0009'; then
    red "未检测到 USBCANFD 设备 (3068:0009)，跳过"
    exit 3
fi

TESTS=(
    device_smoke
    test_rx_pipeline
    device_send_test
    device_filter_test
    device_merge_test
    device_multichannel_test
    device_replay_test
    device_stress_test
)

pass=0; fail=0
declare -a results
for t in "${TESTS[@]}"; do
    [ -x "$BUILD/$t" ] || { results+=("SKIP  $t (未构建)"); continue; }
    info "▶ $t"
    start=$(date +%s)
    timeout "$TIMEOUT" "$BUILD/$t"
    rc=$?
    end=$(date +%s)
    dt=$((end - start))
    if [ "$rc" -eq 0 ]; then
        green "  ✔ 通过 (${dt}s)"; pass=$((pass + 1)); results+=("PASS  $t (${dt}s)")
    else
        red "  ✘ 失败 rc=$rc (${dt}s)"; fail=$((fail + 1)); results+=("FAIL  $t rc=$rc (${dt}s)")
    fi
done

echo
info "==== 实机测试汇总 ===="
for r in "${results[@]}"; do echo "  $r"; done
echo "  通过=$pass 失败=$fail"
[ "$fail" -eq 0 ] || exit 1
green "全部实机测试通过"
