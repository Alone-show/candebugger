#!/usr/bin/env bash
# 把 CanDebugger 打包成自包含的 .deb（自带 Qt5 运行库 / 插件 / QML 模块）
#
# 用法：
#   scripts/package_deb.sh                 # 需要 build-release/CanDebugger 已存在，否则自动构建
#   BUILD_DIR=build-release scripts/package_deb.sh
#   MAINTAINER="Name <mail>" scripts/package_deb.sh
#   STRIP=0 scripts/package_deb.sh           # 跳过去符号（默认 STRIP=1）
#   NO_BUILD=1 scripts/package_deb.sh        # 跳过构建，复用现有二进制
#   DEB_COMPRESS="-Zzstd -z19" scripts/package_deb.sh   # 换压缩（默认 -Zxz -z9）
#
# 产物：dist/candebugger_<version>_<arch>.deb
# 安装布局：/opt/candebugger/{bin,lib,plugins,qml} + /usr/bin/candebugger（包装脚本）
#           + 桌面菜单项 + /usr/lib/udev/rules.d/50-usbcanfd.rules
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-release}"
DIST="$ROOT/dist"

PKGNAME="candebugger"
VERSION="$(sed -n 's/^project(CanDebugger VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
VERSION="${VERSION:-0.1.0}"
ARCH="$(dpkg --print-architecture)"
MAINTAINER="${MAINTAINER:-CanDebugger maintainers <noreply@example.com>}"
# deb 压缩方式：dpkg-deb 1.22 默认 zstd -19；xz -9 更小约 9%（代价是打包/安装稍慢）。
# 想换回默认：DEB_COMPRESS="-Zzstd -z19"
DEB_COMPRESS="${DEB_COMPRESS:--Zxz -z9}"

QT_QML_DIR="/usr/lib/x86_64-linux-gnu/qt5/qml"
QT_PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/qt5/plugins"

STAGE="$BUILD_DIR/pkg"
APPDIR="$STAGE/opt/$PKGNAME"
DEBIAN="$STAGE/DEBIAN"
OUT_DEB="$DIST/${PKGNAME}_${VERSION}_${ARCH}.deb"

info() { printf '\033[36m[package]\033[0m %s\n' "$*"; }
warn() { printf '\033[33m[package]\033[0m %s\n' "$*"; }
die() { printf '\033[31m[package]\033[0m %s\n' "$*" >&2; exit 1; }

command -v dpkg-deb >/dev/null || die "缺少 dpkg-deb（Debian/Ubuntu 自带）"

# ---------------------------------------------------------------- 1. 构建
# 每次打包都跑一遍（增量，很快）：避免源文件/CMakeLists 改了却打进出旧二进制的陈旧包。
# 需要复用现成构建、跳过构建时：NO_BUILD=1 scripts/package_deb.sh
if [ "${NO_BUILD:-0}" = "1" ]; then
    info "按 NO_BUILD=1 跳过构建，直接使用 $BUILD_DIR"
else
    info "Release 构建：$BUILD_DIR"
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCANDBG_BUILD_TESTS=OFF
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi
[ -x "$BUILD_DIR/CanDebugger" ] || die "构建产物缺失：$BUILD_DIR/CanDebugger"

# ---------------------------------------------------------------- 2. staging 骨架
info "清理 staging：$STAGE"
rm -rf "$STAGE"
mkdir -p "$APPDIR"/{bin,lib,plugins,qml} "$DEBIAN"
mkdir -p "$STAGE/usr/bin" \
         "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/icons/hicolor/256x256/apps" \
         "$STAGE/usr/lib/udev/rules.d"

cp "$BUILD_DIR/CanDebugger" "$APPDIR/bin/CanDebugger"
cp "$ROOT/packaging/qt.conf" "$APPDIR/bin/qt.conf"
cp "$ROOT/packaging/qtquickcontrols2.conf" "$APPDIR/bin/qtquickcontrols2.conf"

# 系统文件来源（后续据此计算 Depends）；只记录从系统拷进来的 ELF/模块
SYS_SOURCES=()

# ---------------------------------------------------------------- 3. 私有 SDK 库
# 仓库里 libusbcanfd.so 是真实文件、.1.0.15 是软链；而 SONAME 为 libusbcanfd.so.1.0.15，
# 故落地时反转：真实文件放到 .1.0.15（跟随软链），再建 libusbcanfd.so -> .1.0.15。
cp -L "$ROOT/lib/libusbcanfd.so.1.0.15" "$APPDIR/lib/libusbcanfd.so.1.0.15"
ln -sf libusbcanfd.so.1.0.15 "$APPDIR/lib/libusbcanfd.so"
info "私有库：libusbcanfd.so.1.0.15"

# ---------------------------------------------------------------- 4. Qt 平台/图像插件
copy_plugin_dir() {
    local name="$1"; shift
    local src="$QT_PLUGIN_DIR/$name"
    [ -d "$src" ] || { warn "缺少插件目录：$src"; return 0; }
    local dst="$APPDIR/plugins/$name" f pat
    mkdir -p "$dst"
    for pat in "$@"; do
        for f in "$src"/$pat; do
            [ -e "$f" ] || continue
            cp -a "$f" "$dst/"
            SYS_SOURCES+=("$f")
        done
    done
    info "plugins/$name: $(find "$dst" -maxdepth 1 -type f | wc -l) 个文件"
}

copy_plugin_dir platforms libqxcb.so 'libqwayland-*.so' libqoffscreen.so libqminimal.so
copy_plugin_dir xcbglintegrations '*.so'
copy_plugin_dir wayland-decoration-client '*.so'
copy_plugin_dir wayland-graphics-integration-client '*.so'
copy_plugin_dir wayland-shell-integration '*.so'
# 说明：不打包 imageformats / iconengines —— 程序不使用 QIcon / QML Image，
# 带上它们会把 libQt5Svg -> libQt5Widgets 以及 KDE 的 KIconEnginePlugin（依赖 libKF5*）拖进来。

# ---------------------------------------------------------------- 5. QML 模块
copy_qml_module() {
    local rel="$1"
    local src="$QT_QML_DIR/$rel" dst="$APPDIR/qml/$rel" f
    [ -d "$src" ] || { warn "缺少 QML 模块：$src"; return 0; }
    mkdir -p "$dst"
    while IFS= read -r f; do
        case "$f" in
            */designer/*|*VirtualKeyboard*) continue ;;
            */org.kde.desktop/*|*/Fusion/*|*/Imagine/*|*/Material/*|*/Universal/*) continue ;;
        esac
        mkdir -p "$dst/$(dirname "$f")"
        cp -a "$src/$f" "$dst/$f"
        case "$f" in *.so) SYS_SOURCES+=("$src/$f") ;; esac
    done < <(cd "$src" && find . -type f)
    info "qml/$rel: $(find "$dst" -type f | wc -l) 个文件"
}

copy_qml_module "QtQuick.2"
copy_qml_module "QtQml"
for m in Controls.2 Layouts Templates.2 Window.2; do
    copy_qml_module "QtQuick/$m"
done

# ---------------------------------------------------------------- 6. 收集自带运行库
# 规则：libQt5*（Qt 本体）+ libicu*（Qt5Core 硬依赖，见下）。
# ICU 必须随包自带：ICU 符号带版本后缀（u_strToUpper_76），且 Qt5Core 在 main 之前
# 就调用它，所以既不能省、也不能指向别的 ICU 大版本。
copy_lib() {
    local lib="$1" base tgt tbase
    [ -e "$lib" ] || return 0
    base="$(basename "$lib")"
    if [ ! -e "$APPDIR/lib/$base" ]; then
        cp -a "$lib" "$APPDIR/lib/$base"
    fi
    if [ -L "$lib" ]; then
        tgt="$(readlink -f "$lib")"
        tbase="$(basename "$tgt")"
        if [ "$tbase" != "$base" ] && [ ! -e "$APPDIR/lib/$tbase" ]; then
            cp -a "$tgt" "$APPDIR/lib/$tbase"
        fi
    fi
}

for src in "${SYS_SOURCES[@]}" "$BUILD_DIR/CanDebugger"; do
    while IFS= read -r lib; do
        [ -n "$lib" ] || continue
        case "$lib" in /*) ;; *) continue ;; esac
        case "$(basename "$lib")" in
            libQt5*|libicu*) ;;   # libusbcanfd 已在第 3 步单独落地，避免软链自指
            *) continue ;;
        esac
        copy_lib "$lib"
    done < <(ldd "$src" 2>/dev/null | awk '/=>/ {print $3}')
done
info "自带运行库：$(find "$APPDIR/lib" -maxdepth 1 -type f | wc -l) 个文件"

# ---------------------------------------------------------------- 6.5 去符号
# 说明：Ubuntu 自带的 Qt 库 / 插件本就 strip 过（无 .symtab），实际只有主程序与
# 厂商 SDK 库有符号可去（约 170KB）。用 --strip-unneeded 保留 .dynsym，动态加载不受影响。
if [ "${STRIP:-1}" = "1" ]; then
    if command -v strip >/dev/null 2>&1; then
        stripped=0
        while IFS= read -r f; do
            # ELF 魔数 7f 45 4c 46
            [ "$(head -c 4 "$f" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || continue
            if strip -p --strip-unneeded "$f" 2>/dev/null; then
                stripped=$((stripped + 1))
            fi
        done < <(find "$APPDIR" -type f)
        info "去符号：处理 $stripped 个 ELF"
    else
        warn "未找到 strip，跳过去符号（可 apt install binutils）"
    fi
else
    info "按 STRIP=0 跳过去符号"
fi

# ---------------------------------------------------------------- 7. 图标 / desktop / 包装脚本 / udev
python3 "$ROOT/packaging/make_icon.py" \
    "$STAGE/usr/share/icons/hicolor/256x256/apps/candebugger.png" >/dev/null
cp "$ROOT/packaging/candebugger.desktop" "$STAGE/usr/share/applications/candebugger.desktop"
install -m 0755 "$ROOT/packaging/candebugger" "$STAGE/usr/bin/candebugger"
cp "$ROOT/udev/50-usbcanfd.rules" "$STAGE/usr/lib/udev/rules.d/50-usbcanfd.rules"

# ---------------------------------------------------------------- 8. 计算 Depends
# 用「直接 DT_NEEDED」而不是 ldd 的全传递闭包：
#   1) 依赖更少更稳，不会把 ICU/DBus 的深层传递依赖（libkrb5-*、libgssapi-* 等）写进来；
#   2) 包内自带的库（Qt5 / ICU / libusbcanfd）自动不算依赖，只看它们自己的 NEEDED；
#   3) 真正需要的东西一个不漏 —— 插件、QML 插件也在扫描范围内。
info "解析系统依赖（DT_NEEDED + dpkg -S）..."

# soname -> 系统路径（把 NEEDED 里的名字解析成文件）
declare -A SONAME_PATH=()
if command -v ldconfig >/dev/null 2>&1; then
    while read -r soname rest; do
        [ -n "$soname" ] || continue
        case "$rest" in *"=> "*) ;; *) continue ;; esac
        path="${rest##*=> }"
        # ldconfig -p 同时列出 i386 与 x86_64，只保留本机架构的条目，
        # 否则 libstdc++.so.6 之类会被 32 位路径覆盖，解析成 lib32stdc++6。
        case "$ARCH" in
            amd64) case "$path" in */x86_64-linux-gnu/*|/lib64/*|/usr/lib64/*) ;; *) continue ;; esac ;;
            arm64) case "$path" in */aarch64-linux-gnu/*) ;; *) continue ;; esac ;;
        esac
        SONAME_PATH["$soname"]="$path"
    done < <(ldconfig -p 2>/dev/null)
fi

resolve_soname() {
    local soname="$1" d
    [ -e "$APPDIR/lib/$soname" ] && { printf '%s\n' "$APPDIR/lib/$soname"; return 0; }
    if [ -n "${SONAME_PATH[$soname]:-}" ] && [ -e "${SONAME_PATH[$soname]}" ]; then
        printf '%s\n' "${SONAME_PATH[$soname]}"; return 0
    fi
    for d in /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib64 /usr/lib /lib; do
        if [ -e "$d/$soname" ]; then printf '%s\n' "$d/$soname"; return 0; fi
    done
    return 1
}

declare -A PKGS=()
add_pkg_for_path() {
    local rp owner
    rp="$(realpath -m "$1")"
    owner="$(dpkg -S "$rp" 2>/dev/null | head -1)" || return 0
    [ -n "$owner" ] || return 0
    owner="${owner%%:*}"
    [ "$owner" = "$PKGNAME" ] && return 0
    PKGS["$owner"]=1
}

elf_count=0
unresolved=0
while IFS= read -r f; do
    [ "$(head -c 4 "$f" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || continue
    elf_count=$((elf_count + 1))
    while IFS= read -r needed; do
        [ -n "$needed" ] || continue
        [ -e "$APPDIR/lib/$needed" ] && continue          # 已自带
        if resolved="$(resolve_soname "$needed")"; then
            add_pkg_for_path "$resolved"
        else
            warn "无法解析 NEEDED：$needed（来自 ${f#"$APPDIR"/}）"
            unresolved=$((unresolved + 1))
        fi
    done < <(readelf -d "$f" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p')
done < <(find "$APPDIR" -type f)
info "扫描 $elf_count 个 ELF，未解析 NEEDED $unresolved 个"
[ "$unresolved" = 0 ] || warn "存在未解析的 NEEDED，Depends 可能不完整"

# 跨发行版别名：只改包名、不改 SONAME 的那类（t64 过渡、libffi 大版本）
declare -A DEP_ALIAS=(
    [libglib2.0-0t64]="libglib2.0-0 | libglib2.0-0t64"
    [libglib2.0-0]="libglib2.0-0 | libglib2.0-0t64"
    [libpng16-16t64]="libpng16-16 | libpng16-16t64"
    [libpng16-16]="libpng16-16 | libpng16-16t64"
    [libffi8]="libffi8 | libffi7"
)
declare -A DEP_OUT=()
while IFS= read -r d; do
    [ -n "$d" ] || continue
    case "$d" in
        libicu*) warn "ICU 竟然进了 Depends（应已自带），请检查"; continue ;;
    esac
    DEP_OUT["${DEP_ALIAS[$d]:-$d}"]=1
done < <(printf '%s\n' "${!PKGS[@]}" | sort -u)

DEPENDS="$(printf '%s\n' "${!DEP_OUT[@]}" | sort -u | paste -sd, - | sed 's/,/, /g')"
[ -n "$DEPENDS" ] || die "依赖解析为空，脚本需要修正"
info "Depends: $DEPENDS"

# ---------------------------------------------------------------- 9. control / 权限 / md5sums
INSTALLED_SIZE="$(du -sk --exclude=DEBIAN "$STAGE" | cut -f1)"
sed -e "s/@VERSION@/$VERSION/g" \
    -e "s/@ARCH@/$ARCH/g" \
    -e "s/@MAINTAINER@/$MAINTAINER/g" \
    -e "s/@INSTALLED_SIZE@/$INSTALLED_SIZE/g" \
    -e "s/@DEPENDS@/$DEPENDS/g" \
    "$ROOT/packaging/control.in" > "$DEBIAN/control"

install -m 0755 "$ROOT/packaging/postinst" "$DEBIAN/postinst"
install -m 0755 "$ROOT/packaging/postrm" "$DEBIAN/postrm"

( cd "$STAGE" && find . -type f -not -path './DEBIAN/*' -exec md5sum {} + | sort -k2 ) \
    > "$DEBIAN/md5sums"

# ---------------------------------------------------------------- 10. 构建 deb
mkdir -p "$DIST"
info "构建 $OUT_DEB（压缩：$DEB_COMPRESS）"
# shellcheck disable=SC2086  # DEB_COMPRESS 有意按空格拆分
dpkg-deb --root-owner-group $DEB_COMPRESS --build "$STAGE" "$OUT_DEB" >/dev/null

info "完成：$OUT_DEB"
dpkg-deb -I "$OUT_DEB" | sed 's/^/    /'
printf '\n    安装：sudo dpkg -i %s\n' "$OUT_DEB"
printf '    卸载：sudo dpkg -r %s\n' "$PKGNAME"
