#!/usr/bin/env python3
"""生成 CanDebugger 应用图标（256x256 PNG）。

配色取自 QML 主题（src/qml/Theme.qml）：深色底 + 天蓝强调色 + 绿色通道色。
不依赖仓库内的任何美术资源，纯 PIL 绘制。

用法：python3 packaging/make_icon.py <输出路径.png>
"""
import sys

from PIL import Image, ImageDraw, ImageFont

BG = (15, 23, 42, 255)        # theme.panel   #0f172a
BORDER = (42, 59, 83, 255)    # theme.borderLight #2a3b53
ACCENT = (56, 189, 248, 255)  # theme.accent  #38bdf8
CH1 = (16, 185, 129, 255)     # theme.ch1     #10b981
CH2 = (245, 158, 11, 255)     # theme.ch2     #f59e0b
DIM = (148, 163, 184, 255)    # theme.textDim #94a3b8

SIZE = 256
SS = 4  # 超采样倍数，先 4x 再缩小，得到平滑边缘

FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def rounded_mask(size, radius, ss):
    m = Image.new("L", (size * ss, size * ss), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle(
        (0, 0, size * ss - 1, size * ss - 1), radius=radius * ss, fill=255
    )
    return m


def main(out_path):
    s = SIZE * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # 圆角底板 + 描边
    d.rounded_rectangle((0, 0, s - 1, s - 1), radius=48 * SS, fill=BG)
    d.rounded_rectangle(
        (3 * SS, 3 * SS, s - 4 * SS, s - 4 * SS),
        radius=46 * SS,
        outline=BORDER,
        width=3 * SS,
    )

    # 左：CAN 总线示意（一条主干 + 三个节点支线）
    bus_y = 74 * SS
    d.line((30 * SS, bus_y, 226 * SS, bus_y), fill=DIM, width=4 * SS)
    for i, x in enumerate((66, 128, 190)):
        color = (CH1, ACCENT, CH2)[i]
        d.line((x * SS, bus_y, x * SS, bus_y + 26 * SS), fill=color, width=4 * SS)
        r = 9 * SS
        d.ellipse(
            (x * SS - r, bus_y + 26 * SS, x * SS + r, bus_y + 26 * SS + 2 * r),
            fill=color,
        )

    # 中：CAN 主字
    font = ImageFont.truetype(FONT_BOLD, 84 * SS)
    text = "CAN"
    box = d.textbbox((0, 0), text, font=font)
    tw, th = box[2] - box[0], box[3] - box[1]
    d.text(
        ((s - tw) / 2 - box[0], 150 * SS - box[1]),
        text,
        font=font,
        fill=(226, 232, 240, 255),
    )

    # 右下：FD 徽标
    font_sm = ImageFont.truetype(FONT_BOLD, 40 * SS)
    badge = d.textbbox((0, 0), "FD", font=font_sm)
    bw = badge[2] - badge[0] + 20 * SS
    bh = badge[3] - badge[1] + 12 * SS
    bx, by = s - bw - 18 * SS, s - bh - 16 * SS
    d.rounded_rectangle((bx, by, bx + bw, by + bh), radius=10 * SS, fill=ACCENT)
    d.text(
        (bx + 10 * SS - badge[0], by + 6 * SS - badge[1]),
        "FD",
        font=font_sm,
        fill=(4, 18, 31, 255),
    )

    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    img.putalpha(rounded_mask(SIZE, 48, 1))
    img.save(out_path, "PNG")
    print(f"icon written: {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("用法: make_icon.py <输出.png>")
    main(sys.argv[1])
