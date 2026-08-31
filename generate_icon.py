from pathlib import Path
from PIL import Image, ImageDraw

output = Path(__file__).with_name("mood-kid.ico")
sizes = [16, 24, 32, 48, 64, 128, 256]
images = []
for size in sizes:
    scale = size / 24.0
    canvas = size * 4
    image = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    def point(x, y):
        return (x * scale * 4, y * scale * 4)
    width = max(2, round(2 * scale * 4))
    draw.ellipse([point(3, 3), point(21, 21)], outline=(32, 32, 32, 255), width=width)
    dot = max(2, round(0.9 * scale * 4))
    draw.ellipse([point(8.55, 9.55), point(9.45, 10.45)], fill=(32, 32, 32, 255))
    draw.ellipse([point(14.55, 9.55), point(15.45, 10.45)], fill=(32, 32, 32, 255))
    draw.arc([point(9.5, 11.5), point(14.5, 16.5)], 45, 135, fill=(32, 32, 32, 255), width=width)
    draw.arc([point(10, 1), point(14, 7)], 90, 270, fill=(32, 32, 32, 255), width=width)
    image = image.resize((size, size), Image.Resampling.LANCZOS)
    images.append(image)
images[0].save(output, format="ICO", sizes=[(s, s) for s in sizes], append_images=images[1:])
print(output)
