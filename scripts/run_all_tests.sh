#!/usr/bin/env bash
# CanDebugger 分级测试编排（L1 单元 + L2 QML + L4 sanitizer/覆盖率）
#
# 用法：
#   scripts/run_all_tests.sh [release|asan|tsan|coverage|all]
# 环境：BUILD_DIR / JOBS 可覆盖
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JOBS="${JOBS:-$(nproc)}"
MODE="${1:-release}"

red()   { printf '\033[31m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
info()  { printf '\033[36m%s\033[0m\n' "$*"; }

configure_build() {
    local dir="$1"; shift
    cmake -B "$dir" -DCMAKE_BUILD_TYPE="${BT:-Release}" "$@" >/dev/null || return 1
    cmake --build "$dir" -j"$JOBS" >/dev/null || return 1
}

run_release() {
    local dir="$ROOT/build"
    info "[release] 配置并构建 $dir"
    configure_build "$dir" || { red "构建失败"; return 1; }
    ctest --test-dir "$dir" --timeout 120 --output-on-failure
}

run_asan() {
    local dir="$ROOT/build-asan"
    info "[asan] 构建 $dir（address,undefined）"
    BT=Debug configure_build "$dir" -DCANDBG_SANITIZE=address,undefined || { red "构建失败"; return 1; }
    # Qt 运行时可能产生已知误报，关闭 leak 检测；重点看越界/UAF/UB
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
        ctest --test-dir "$dir" --timeout 300 --output-on-failure
}

run_tsan() {
    local dir="$ROOT/build-tsan"
    info "[tsan] 构建 $dir（thread）"
    BT=Debug configure_build "$dir" -DCANDBG_SANITIZE=thread || { red "构建失败"; return 1; }
    # TSan 仅跑无设备的纯并发用例，避免预编译 SDK 造成噪声；缩小规模加速
    export CANDBG_TEST_DIV="${CANDBG_TEST_DIV:-20}"
    export TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1"
    ctest --test-dir "$dir" --timeout 300 -R "ring_concurrency|filter_proxy|buffer_model" --output-on-failure
}

run_coverage() {
    local dir="$ROOT/build-cov"
    info "[coverage] 构建 $dir（gcov）"
    BT=Debug configure_build "$dir" -DCANDBG_COVERAGE=ON || { red "构建失败"; return 1; }
    ctest --test-dir "$dir" --timeout 120 >/dev/null 2>&1

    if command -v lcov >/dev/null 2>&1; then
        info "[coverage] lcov 生成报告"
        local ign="mismatch,negative,unused,empty,inconsistent"
        lcov --capture --directory "$dir" --output-file "$dir/coverage.info" \
             --ignore-errors "$ign" >/dev/null 2>&1
        lcov --remove "$dir/coverage.info" '/usr/*' '*/tests/*' '*/build-cov/*' '*/include/*' \
             --output-file "$dir/coverage.filtered.info" --ignore-errors "$ign" >/dev/null 2>&1
        lcov --list "$dir/coverage.filtered.info" --ignore-errors "$ign" 2>/dev/null | tail -40
        lcov --summary "$dir/coverage.filtered.info" --ignore-errors "$ign" 2>&1 | tail -8
    elif command -v gcovr >/dev/null 2>&1; then
        info "[coverage] gcovr 生成报告"
        gcovr -r "$ROOT" --filter "$ROOT/src" --txt -o "$dir/coverage.txt"
        cat "$dir/coverage.txt"
    else
        red "缺少 lcov / gcovr，无法汇总覆盖率（可 apt install lcov）"
        return 0
    fi
}

case "$MODE" in
    release)  run_release ;;
    asan)     run_asan ;;
    tsan)     run_tsan ;;
    coverage) run_coverage ;;
    all)
        run_release && run_asan && run_tsan && run_coverage
        ;;
    *)
        red "未知模式: $MODE（可选 release|asan|tsan|coverage|all）"
        exit 2 ;;
esac

rc=$?
if [ "$rc" -eq 0 ]; then
    green "== $MODE 完成：全部通过 =="
else
    red "== $MODE 存在失败 (rc=$rc) =="
fi
exit "$rc"
