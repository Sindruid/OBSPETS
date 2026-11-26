"""Draws the installer's icon and wizard pictures from Blob's sprite sheet (data/packs/blob/sheet.png).

    python tools/installer/make-art.py

Writes art/obspet.ico, art/wizard-<w>.png (the picture on the first and last page) and
art/small-<w>.png (top right of the progress page), several sizes each so they stay sharp at any
Windows display scale. The sprite is scaled by whole numbers only, so its pixels stay crisp; the
backgrounds are transparent, so they suit both the light and the dark look. Needs Pillow. The
outputs are committed, so packaging doesn't need Pillow; run this again only if Blob changes.
"""
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SHEET = os.path.join(HERE, "..", "..", "data", "packs", "blob", "sheet.png")
OUT = os.path.join(HERE, "art")
CELL = 32


def frame(col, row):
    im = Image.open(SHEET).convert("RGBA")
    cell = im.crop((col * CELL, row * CELL, (col + 1) * CELL, (row + 1) * CELL))
    return cell.crop(cell.getbbox())


def scaled(sprite, k):
    return sprite.resize((sprite.width * k, sprite.height * k), Image.NEAREST)


def shadow(width, k):
    """A soft pixel-step ellipse under the feet, k screen pixels per sprite pixel."""
    rows = [(0.9, 70), (0.7, 40)]   # (width share, alpha)
    im = Image.new("RGBA", (width, len(rows) * k), (0, 0, 0, 0))
    for i, (share, a) in enumerate(rows):
        w = int(width * share) // k * k
        x = (width - w) // 2
        im.paste(Image.new("RGBA", (w, k), (0, 0, 0, a)), (x, i * k))
    return im


def wizard(w, h, k, sprite):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    s = scaled(sprite, k)
    x, y = (w - s.width) // 2, int(h * 0.58) - s.height
    sh = shadow(s.width, k)
    im.alpha_composite(sh, (x, y + s.height - k))
    im.alpha_composite(s, (x, y))
    return im


def square(size, sprite):
    k = max(1, min(size // sprite.width, size // sprite.height))
    s = scaled(sprite, k)
    if s.width > size:   # 16 px: smaller than the sprite itself
        f = size / sprite.width
        s = sprite.resize((size, max(1, round(sprite.height * f))), Image.LANCZOS)
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    im.alpha_composite(s, ((size - s.width) // 2, (size - s.height) // 2))
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    blob = frame(0, 0)          # idle
    # Modern wizard picture: 164x314 at 100%; 125, 150, 175 and 200%.
    for scale, k in ((100, 5), (125, 6), (150, 7), (175, 9), (200, 10)):
        w, h = 164 * scale // 100, 314 * scale // 100
        wizard(w, h, k, blob).save(os.path.join(OUT, f"wizard-{w}.png"))
    for scale in (100, 125, 150, 175, 200):
        n = 55 * scale // 100
        square(n, blob).save(os.path.join(OUT, f"small-{n}.png"))
    icons = [square(n, blob) for n in (16, 24, 32, 48, 64, 128, 256)]
    icons[-1].save(os.path.join(OUT, "obspet.ico"), sizes=[(i.width, i.height) for i in icons],
                   append_images=icons[:-1])
    print("wrote", ", ".join(sorted(os.listdir(OUT))))


if __name__ == "__main__":
    main()
