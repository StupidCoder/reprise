"""Draws DoubleOCheese's app icon (a cheese wedge on a warm tile, no game artwork) and writes
build/AppIcon.icns with macOS's iconutil. Needs Pillow."""
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

root = Path(__file__).resolve().parents[1]


def draw(size):
    s = size / 1024
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # Rounded tile with a vertical gradient.
    tile = Image.new('RGBA', (size, size))
    for y in range(size):
        t = y / (size - 1)
        tile.paste((int(255 - 5 * t), int(222 - 50 * t), int(120 - 60 * t), 255), (0, y, size, y + 1))
    mask = Image.new('L', (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((int(100 * s), int(100 * s), int(924 * s), int(924 * s)),
                                           radius=int(185 * s), fill=255)
    im.paste(tile, (0, 0), mask)
    # Wedge: top face, front face (rind), holes.
    shadow = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).polygon([(230 * s, 700 * s), (820 * s, 380 * s), (820 * s, 740 * s), (230 * s, 760 * s)],
                                   fill=(90, 50, 10, 90))
    im.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(18 * s)), (0, int(18 * s)))
    d = ImageDraw.Draw(im)
    d.polygon([(220 * s, 640 * s), (800 * s, 330 * s), (800 * s, 560 * s)], fill=(255, 214, 92, 255))
    d.polygon([(220 * s, 640 * s), (800 * s, 560 * s), (800 * s, 720 * s), (220 * s, 740 * s)], fill=(244, 170, 46, 255))
    for cx, cy, r in [(600, 470, 46), (690, 640, 34), (430, 670, 26), (520, 560, 22)]:
        d.ellipse(((cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s), fill=(214, 140, 30, 255))
    return im


with tempfile.TemporaryDirectory() as temp:
    iconset = Path(temp) / 'AppIcon.iconset'
    iconset.mkdir()
    for points in [16, 32, 128, 256, 512]:
        draw(points).save(iconset / f'icon_{points}x{points}.png')
        draw(points * 2).save(iconset / f'icon_{points}x{points}@2x.png')
    (root / 'build').mkdir(exist_ok=True)
    subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o', str(root / 'build' / 'AppIcon.icns')], check=True)
print('Icon:', root / 'build' / 'AppIcon.icns')
