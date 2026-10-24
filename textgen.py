from PIL import Image, ImageDraw, ImageFont
import os

os.makedirs("textures", exist_ok=True)

# 1. Create a 256x128 IA8-compatible Grayscale Font Sheet (16x8 grid)
font_img = Image.new("LA", (256, 128), (0, 0)) # L = Luminance, A = Alpha
draw = ImageDraw.Draw(font_img)
font = ImageFont.load_default()

for ascii_code in range(32, 128):
    col = ascii_code % 16
    row = ascii_code // 16
    x = col * 16 + 4
    y = row * 16 + 2
    draw.text((x, y), chr(ascii_code), fill=(255, 255), font=font)

font_img.save("textures/font.png")

# 2. Create a 160x64 IA8 Placeholder Logo
logo_img = Image.new("LA", (160, 64), (40, 255))
ldraw = ImageDraw.Draw(logo_img)
ldraw.rectangle([0, 0, 159, 63], outline=(200, 255), width=2)
ldraw.text((38, 26), "LOGO", fill=(255, 255), font=font)
logo_img.save("textures/logo.png")

print("Updated textures for IA8 format!")