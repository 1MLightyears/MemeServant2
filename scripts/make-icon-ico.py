"""Regenerate src/resources/icon.ico from src/resources/icon.png.

Windows 资源编译器只接受 .ico；exe 图标与 Qt 端（qrc 内的 :/icons/icon.ico）
都使用本脚本生成的多尺寸文件。替换图标后请重新运行本脚本并重新构建。
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "resources" / "icon.png"
TARGET = ROOT / "src" / "resources" / "icon.ico"
SIZES = [16, 24, 32, 48, 64, 128, 256]


def main() -> None:
    image = Image.open(SOURCE)
    sizes = [(size, size) for size in SIZES if size <= max(image.size)]
    if not sizes:
        raise SystemExit(f"source image is smaller than {SIZES[0]} px: {image.size}")
    image.save(TARGET, bitmap_format="bmp", sizes=sizes)
    print(f"wrote {TARGET} with entries {sizes}")


if __name__ == "__main__":
    main()
