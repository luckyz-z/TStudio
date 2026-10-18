from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
LOGO = Image.open(ROOT / "Resources" / "logo.png").convert("RGBA")
OUT = ROOT / "installer"


def font(size, bold=True):
    for name in (["segoeuib.ttf", "arialbd.ttf"] if bold else ["segoeui.ttf", "arial.ttf"]):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def gradient(size, top, bottom):
    image = Image.new("RGB", size, top)
    draw = ImageDraw.Draw(image)
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        draw.line([(0, y), (size[0], y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return image


def glow(image, centre, radius, colour):
    layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).ellipse([centre[0] - radius, centre[1] - radius, centre[0] + radius, centre[1] + radius], fill=colour)
    layer = layer.filter(ImageFilter.GaussianBlur(radius * 0.45))
    return Image.alpha_composite(image.convert("RGBA"), layer)


def wizard(width, height):
    scale = width / 164
    image = gradient((width, height), (43, 49, 53), (16, 19, 21))
    image = glow(image, (width // 2, int(height * 0.36)), int(70 * scale), (232, 72, 59, 90))
    size = int(118 * scale)
    image.alpha_composite(LOGO.resize((size, size), Image.LANCZOS), ((width - size) // 2, int(height * 0.36 - size / 2)))
    draw = ImageDraw.Draw(image)
    title = font(int(26 * scale))
    draw.text((width // 2, int(height * 0.6)), "TStudio", font=title, fill=(214, 221, 224), anchor="mm")
    small = font(int(10 * scale), bold=False)
    for i, line in enumerate(["open-source DAW", "made by producers"]):
        draw.text((width // 2, int(height * 0.6 + (24 + 14 * i) * scale)), line, font=small, fill=(127, 139, 144), anchor="mm")
    draw.rectangle([0, height - int(5 * scale), width, height], fill=(232, 72, 59))
    return image.convert("RGB")


def small(size):
    image = gradient((size, size), (43, 49, 53), (24, 28, 30)).convert("RGBA")
    logo = LOGO.resize((int(size * 0.86), int(size * 0.86)), Image.LANCZOS)
    image.alpha_composite(logo, ((size - logo.width) // 2, (size - logo.height) // 2))
    return image.convert("RGB")


def banner(width=1600, height=520):
    image = gradient((width, height), (36, 41, 45), (14, 16, 18)).convert("RGBA")
    image = glow(image, (int(width * 0.2), height // 2), 230, (232, 72, 59, 110))
    shot = Image.open(ROOT / "docs" / "images" / "main.png").convert("RGBA")
    shot = shot.resize((int(height * 1.32 * shot.width / shot.height), int(height * 1.32)), Image.LANCZOS)
    fade = Image.new("L", shot.size, 0)
    fade_draw = ImageDraw.Draw(fade)
    for x in range(shot.width):
        fade_draw.line([(x, 0), (x, shot.height)], fill=int(min(1.0, x / (shot.width * 0.35)) * 200))
    shot.putalpha(fade)
    image.alpha_composite(shot, (int(width * 0.48), int(height * 0.12)))
    logo = LOGO.resize((150, 150), Image.LANCZOS)
    image.alpha_composite(logo, (90, height // 2 - 150))
    draw = ImageDraw.Draw(image)
    draw.text((260, height // 2 - 75), "TStudio", font=font(96), fill=(236, 240, 242), anchor="lm")
    draw.text((92, height // 2 + 40), "The FL Studio workflow. Free and open source.", font=font(34), fill=(214, 221, 224), anchor="lm")
    draw.text((92, height // 2 + 88), "Lua plugins with custom UIs  •  VST3  •  AI control over MCP", font=font(24, bold=False), fill=(150, 160, 165), anchor="lm")
    draw.rectangle([0, height - 8, width, height], fill=(232, 72, 59))
    return image.convert("RGB")


OUT.mkdir(exist_ok=True)
for width, height in [(164, 314), (192, 386), (246, 459), (273, 556), (328, 604), (410, 797)]:
    wizard(width, height).save(OUT / f"wizard-{width}.bmp")
for size in [55, 64, 69, 92, 110, 119, 138]:
    small(size).save(OUT / f"small-{size}.bmp")
LOGO.save(OUT / "icon.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
banner().save(ROOT / "docs" / "images" / "banner.png", optimize=True)
print("installer art and banner written")
