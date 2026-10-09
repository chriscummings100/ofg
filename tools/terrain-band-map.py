"""Render exported production-controller cuts as labelled observer-centered LOD maps."""
from pathlib import Path
import csv
import sys
import colorsys
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
folder = root / "artifacts/terrain/bands"

if "--large" in sys.argv:
    colors = [tuple(round(255 * v) for v in colorsys.hsv_to_rgb(d / 14, .65, .9)) for d in range(14)]
    image = Image.new("RGB", (1648, 1060), "#171b23")
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 20)
    small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 16)
    draw.text((24, 12), "131,072 m roots to 16 m cells: actual settled cuts from the independent radial-band tests", font=font, fill="white")
    draw.text((24, 42), "Flat surface at Y=3 m; observer Y=5 m. White dot = observer. Fixture refinement range is unlimited.", font=small, fill="white")
    for row_index, (name, observer_x) in enumerate((("offset", 123), ("centred", 65536))):
        rows = list(csv.DictReader((folder / f"large-{name}.csv").open()))
        observer_z = observer_x + 108
        top = 114 + row_index * 416
        draw.text((24, top - 35), f"Observer ({observer_x}, 5, {observer_z}) m", font=font, fill="white")
        for panel, radius in enumerate((524288, 32768, 2048, 128)):
            left, side = 24 + panel * 404, 380
            tile = Image.new("RGB", (side, side), "#11151b")
            pen = ImageDraw.Draw(tile)
            scale = side / (2 * radius)
            for row in rows:
                x, z, width = (float(row[k]) for k in ("x", "z", "width"))
                x0, x1 = ((x - observer_x + radius) * scale, (x + width - observer_x + radius) * scale)
                z0, z1 = ((radius - z - width + observer_z) * scale, (radius - z + observer_z) * scale)
                if x1 < 0 or z1 < 0 or x0 >= side or z0 >= side:
                    continue
                pen.rectangle((x0, z0, x1, z1), fill=colors[int(row["depth"])], outline="#272b34")
            center = side / 2
            pen.ellipse((center-4, center-4, center+4, center+4), fill="white", outline="black", width=1)
            image.paste(tile, (left, top))
            draw.text((left + 8, top + 8), f"{2 * radius:,} m across", font=small, fill="white", stroke_width=2, stroke_fill="black")
    for depth, color in enumerate(colors):
        x, y = 24 + (depth % 7) * 230, 958 + (depth // 7) * 38
        draw.rectangle((x, y, x+20, y+20), fill=color)
        draw.text((x+28, y), f"D{depth}: {131072 >> depth:,} m", font=small, fill="white")
    image.save(folder / "large-bands.png")
    print(folder / "large-bands.png")
    sys.exit(0)

rows = list(csv.DictReader((folder / "cut.csv").open()))
colors = ["#cc3333", "#cc801a", "#b3cc1a", "#1ab34d", "#1a80e6", "#9933e6", "#e633b3"]
image = Image.new("RGB", (1656, 670), "#171b23")
draw = ImageDraw.Draw(image)
font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 20)
small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 16)
draw.text((24, 12), "Actual streamed cut on a flat surface: observer at (123, 5, 231), surface height 3 m", font=font, fill="white")
for panel, radius in enumerate((4096, 512, 64)):
    left, top, side = 24 + panel * 544, 80, 520
    tile = Image.new("RGB", (side, side), "#11151b")
    pen = ImageDraw.Draw(tile)
    scale = side / (2 * radius)
    for row in rows:
        x, z, width = (float(row[k]) for k in ("x", "z", "width"))
        x0, x1 = ((x - 123 + radius) * scale, (x + width - 123 + radius) * scale)
        z0, z1 = ((radius - z - width + 231) * scale, (radius - z + 231) * scale)
        if x1 < 0 or z1 < 0 or x0 >= side or z0 >= side:
            continue
        pen.rectangle((x0, z0, x1, z1), fill=colors[int(row["depth"])], outline="#272b34", width=1)
    center = side / 2
    pen.ellipse((center-5, center-5, center+5, center+5), fill="white", outline="black", width=2)
    image.paste(tile, (left, top))
    draw.text((left, 48), f"{radius * 2:g} m across", font=font, fill="white")
for depth, color in enumerate(colors):
    x = 24 + depth * 220
    draw.rectangle((x, 628, x+22, 650), fill=color)
    draw.text((x+30, 627), f"LOD {6-depth}: {1024 >> depth} m cells", font=small, fill="white")
image.save(folder / "bands.png")
print(folder / "bands.png")
