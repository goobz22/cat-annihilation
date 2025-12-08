#!/usr/bin/env python3
"""Generate all game textures procedurally."""

import os
import random
from PIL import Image, ImageDraw, ImageFilter
import math

# Set random seed for reproducibility
random.seed(42)

def add_noise(img, intensity=20):
    """Add noise to an image."""
    pixels = img.load()
    width, height = img.size
    for y in range(height):
        for x in range(width):
            if img.mode == 'RGB':
                r, g, b = pixels[x, y]
                noise = random.randint(-intensity, intensity)
                pixels[x, y] = (
                    max(0, min(255, r + noise)),
                    max(0, min(255, g + noise)),
                    max(0, min(255, b + noise))
                )
            elif img.mode == 'RGBA':
                r, g, b, a = pixels[x, y]
                noise = random.randint(-intensity, intensity)
                pixels[x, y] = (
                    max(0, min(255, r + noise)),
                    max(0, min(255, g + noise)),
                    max(0, min(255, b + noise)),
                    a
                )
    return img

def perlin_noise_texture(width, height, scale=50):
    """Generate simple Perlin-like noise values."""
    noise_map = []
    for y in range(height):
        row = []
        for x in range(width):
            # Simple multi-octave noise simulation
            value = 0
            freq = 1
            amp = 1
            for octave in range(4):
                nx = x / scale * freq
                ny = y / scale * freq
                value += (math.sin(nx) * math.cos(ny) + 1) / 2 * amp
                freq *= 2
                amp *= 0.5
            row.append(value / 2)  # Normalize
        noise_map.append(row)
    return noise_map

def generate_cat_albedo():
    """Generate orange/ginger cat fur pattern."""
    img = Image.new('RGB', (512, 512), (200, 120, 60))
    draw = ImageDraw.Draw(img)

    # Add stripes
    for i in range(20):
        y = random.randint(0, 512)
        width = random.randint(10, 40)
        for dy in range(width):
            if y + dy < 512:
                for x in range(512):
                    if random.random() > 0.3:
                        current = img.getpixel((x, y + dy))
                        darker = tuple(max(0, c - random.randint(30, 60)) for c in current)
                        img.putpixel((x, y + dy), darker)

    img = add_noise(img, 15)
    return img

def generate_dog_albedo():
    """Generate brown/tan dog fur pattern."""
    img = Image.new('RGB', (512, 512), (140, 100, 70))

    # Add lighter patches
    draw = ImageDraw.Draw(img)
    for i in range(30):
        x = random.randint(0, 512)
        y = random.randint(0, 512)
        r = random.randint(20, 60)
        color = (170, 130, 90)
        draw.ellipse([x-r, y-r, x+r, y+r], fill=color)

    img = img.filter(ImageFilter.GaussianBlur(10))
    img = add_noise(img, 20)
    return img

def generate_normal_map(flat=True, detail=0):
    """Generate a normal map. Flat or with some detail."""
    img = Image.new('RGB', (512, 512), (128, 128, 255))

    if not flat and detail > 0:
        pixels = img.load()
        for y in range(512):
            for x in range(512):
                # Add some variation for non-flat surfaces
                variation = random.randint(-detail, detail)
                pixels[x, y] = (
                    max(0, min(255, 128 + variation)),
                    max(0, min(255, 128 + variation)),
                    max(0, min(255, 255 - abs(variation)))
                )

    return img

def generate_grass_texture():
    """Generate green grass texture."""
    img = Image.new('RGB', (512, 512), (60, 120, 40))
    draw = ImageDraw.Draw(img)

    # Add grass blades
    for i in range(500):
        x = random.randint(0, 512)
        y = random.randint(0, 512)
        length = random.randint(5, 15)
        width = random.randint(1, 3)
        color = (
            random.randint(50, 80),
            random.randint(110, 140),
            random.randint(30, 60)
        )
        draw.line([x, y, x + random.randint(-3, 3), y - length], fill=color, width=width)

    img = img.filter(ImageFilter.GaussianBlur(1))
    img = add_noise(img, 10)
    return img

def generate_grass_normal():
    """Generate grass normal map with blade details."""
    return generate_normal_map(flat=False, detail=15)

def generate_dirt_texture():
    """Generate brown dirt/earth texture."""
    img = Image.new('RGB', (512, 512), (100, 70, 50))

    # Add dirt clumps and variation
    draw = ImageDraw.Draw(img)
    for i in range(100):
        x = random.randint(0, 512)
        y = random.randint(0, 512)
        r = random.randint(5, 20)
        color = (
            random.randint(80, 120),
            random.randint(60, 90),
            random.randint(40, 70)
        )
        draw.ellipse([x-r, y-r, x+r, y+r], fill=color)

    img = img.filter(ImageFilter.GaussianBlur(3))
    img = add_noise(img, 25)
    return img

def generate_dirt_normal():
    """Generate dirt normal map."""
    return generate_normal_map(flat=False, detail=10)

def generate_rock_texture():
    """Generate gray rock texture."""
    img = Image.new('RGB', (512, 512), (120, 120, 120))

    # Add cracks and variation
    draw = ImageDraw.Draw(img)
    for i in range(50):
        x1 = random.randint(0, 512)
        y1 = random.randint(0, 512)
        x2 = x1 + random.randint(-50, 50)
        y2 = y1 + random.randint(-50, 50)
        color = (
            random.randint(60, 80),
            random.randint(60, 80),
            random.randint(60, 80)
        )
        draw.line([x1, y1, x2, y2], fill=color, width=random.randint(1, 3))

    img = img.filter(ImageFilter.GaussianBlur(2))
    img = add_noise(img, 30)
    return img

def generate_rock_normal():
    """Generate rocky normal map."""
    return generate_normal_map(flat=False, detail=25)

def generate_tree_bark():
    """Generate brown bark texture."""
    img = Image.new('RGB', (512, 512), (80, 60, 40))
    draw = ImageDraw.Draw(img)

    # Vertical bark lines
    for x in range(0, 512, random.randint(20, 40)):
        offset = random.randint(-10, 10)
        for y in range(512):
            if random.random() > 0.3:
                color = (
                    random.randint(60, 100),
                    random.randint(45, 75),
                    random.randint(30, 50)
                )
                draw.point((x + offset + random.randint(-3, 3), y), fill=color)

    img = img.filter(ImageFilter.GaussianBlur(1))
    img = add_noise(img, 20)
    return img

def generate_tree_leaves():
    """Generate green leaf pattern."""
    img = Image.new('RGBA', (512, 512), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Draw leaf shapes
    for i in range(200):
        x = random.randint(0, 512)
        y = random.randint(0, 512)
        r = random.randint(10, 25)
        color = (
            random.randint(40, 80),
            random.randint(100, 150),
            random.randint(30, 70),
            random.randint(200, 255)
        )
        draw.ellipse([x-r, y-r, x+r, y+r], fill=color)

    img = img.filter(ImageFilter.GaussianBlur(2))
    return img

def generate_skybox_top():
    """Generate light blue sky for top."""
    img = Image.new('RGB', (512, 512), (135, 206, 235))
    img = add_noise(img, 5)
    return img

def generate_skybox_bottom():
    """Generate dark ground color for bottom."""
    img = Image.new('RGB', (512, 512), (50, 40, 30))
    img = add_noise(img, 10)
    return img

def generate_skybox_sides():
    """Generate gradient sky to horizon."""
    img = Image.new('RGB', (512, 512))
    pixels = img.load()

    for y in range(512):
        # Gradient from light blue (top) to lighter near horizon
        factor = y / 512
        r = int(135 + (200 - 135) * factor)
        g = int(206 + (220 - 206) * factor)
        b = int(235 + (240 - 235) * factor)

        for x in range(512):
            pixels[x, y] = (r, g, b)

    img = add_noise(img, 5)
    return img

def generate_ui_health_bar():
    """Generate red health bar."""
    img = Image.new('RGB', (256, 32), (220, 20, 20))
    draw = ImageDraw.Draw(img)
    # Add highlight
    draw.rectangle([0, 0, 256, 8], fill=(255, 100, 100))
    return img

def generate_ui_health_bar_bg():
    """Generate dark background for health bar."""
    img = Image.new('RGB', (256, 32), (40, 40, 40))
    draw = ImageDraw.Draw(img)
    # Add border
    draw.rectangle([0, 0, 255, 31], outline=(80, 80, 80), width=2)
    return img

def generate_ui_button():
    """Generate button texture with border."""
    img = Image.new('RGB', (256, 64), (80, 80, 120))
    draw = ImageDraw.Draw(img)

    # Border
    draw.rectangle([0, 0, 255, 63], outline=(150, 150, 200), width=3)
    # Highlight
    draw.rectangle([3, 3, 252, 20], fill=(100, 100, 140))

    return img

def generate_ui_panel():
    """Generate semi-transparent panel."""
    img = Image.new('RGBA', (512, 512), (40, 40, 50, 200))
    draw = ImageDraw.Draw(img)

    # Border
    draw.rectangle([0, 0, 511, 511], outline=(100, 100, 120, 255), width=4)

    return img

def generate_crosshair():
    """Generate simple crosshair."""
    img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    center = 32
    length = 15
    thickness = 2
    color = (255, 255, 255, 255)

    # Horizontal line
    draw.rectangle([center - length, center - thickness//2,
                   center - 5, center + thickness//2], fill=color)
    draw.rectangle([center + 5, center - thickness//2,
                   center + length, center + thickness//2], fill=color)

    # Vertical line
    draw.rectangle([center - thickness//2, center - length,
                   center + thickness//2, center - 5], fill=color)
    draw.rectangle([center - thickness//2, center + 5,
                   center + thickness//2, center + length], fill=color)

    return img

def generate_particle_spark():
    """Generate white spark/glow particle."""
    img = Image.new('RGBA', (128, 128), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    center = 64
    # Radial gradient effect
    for r in range(40, 0, -2):
        alpha = int(255 * (1 - r/40))
        color = (255, 255, 200, alpha)
        draw.ellipse([center-r, center-r, center+r, center+r], fill=color)

    return img

def generate_particle_smoke():
    """Generate gray smoke puff particle."""
    img = Image.new('RGBA', (128, 128), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    center = 64
    # Soft smoke effect
    for r in range(50, 0, -3):
        alpha = int(128 * (1 - r/50))
        gray = random.randint(100, 150)
        color = (gray, gray, gray, alpha)
        x_offset = random.randint(-5, 5)
        y_offset = random.randint(-5, 5)
        draw.ellipse([center-r+x_offset, center-r+y_offset,
                     center+r+x_offset, center+r+y_offset], fill=color)

    img = img.filter(ImageFilter.GaussianBlur(5))
    return img

def main():
    """Generate all textures."""
    print("Generating textures for Cat Annihilation...")

    textures = [
        # Character Textures
        ("cat_albedo.png", generate_cat_albedo),
        ("cat_normal.png", lambda: generate_normal_map(flat=True)),
        ("dog_albedo.png", generate_dog_albedo),
        ("dog_normal.png", lambda: generate_normal_map(flat=True)),

        # Terrain Textures
        ("terrain_grass.png", generate_grass_texture),
        ("terrain_grass_normal.png", generate_grass_normal),
        ("terrain_dirt.png", generate_dirt_texture),
        ("terrain_dirt_normal.png", generate_dirt_normal),
        ("terrain_rock.png", generate_rock_texture),
        ("terrain_rock_normal.png", generate_rock_normal),

        # Environment
        ("tree_bark.png", generate_tree_bark),
        ("tree_leaves.png", generate_tree_leaves),
        ("skybox_top.png", generate_skybox_top),
        ("skybox_bottom.png", generate_skybox_bottom),
        ("skybox_sides.png", generate_skybox_sides),

        # UI Textures
        ("ui_health_bar.png", generate_ui_health_bar),
        ("ui_health_bar_bg.png", generate_ui_health_bar_bg),
        ("ui_button.png", generate_ui_button),
        ("ui_panel.png", generate_ui_panel),
        ("crosshair.png", generate_crosshair),

        # Effects
        ("particle_spark.png", generate_particle_spark),
        ("particle_smoke.png", generate_particle_smoke),
    ]

    for filename, generator in textures:
        print(f"Generating {filename}...")
        img = generator()
        img.save(filename)
        print(f"  ✓ Saved {filename}")

    print(f"\n✓ Successfully generated {len(textures)} textures!")
    print("\nTexture files created:")
    for filename, _ in textures:
        if os.path.exists(filename):
            size = os.path.getsize(filename)
            print(f"  {filename} ({size:,} bytes)")

if __name__ == "__main__":
    main()
