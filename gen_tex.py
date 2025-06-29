#!/usr/bin/env python3
"""
Enhanced Fruit Texture Generator for Suika Game
Creates full square textures (not just circles) for proper UV mapping
"""

from PIL import Image, ImageDraw, ImageFilter
import os
import math
import random

def create_base_texture(size, base_color, variation=30):
    """Create a base texture with subtle color variation"""
    img = Image.new('RGB', (size, size))
    pixels = img.load()

    for x in range(size):
        for y in range(size):
            # Add noise based on a smooth perlin-like pattern for natural variation
            noise = int(variation * (math.sin(x*0.1) * math.cos(y*0.1)))
            r = max(0, min(255, base_color[0] + noise + random.randint(-variation//3, variation//3)))
            g = max(0, min(255, base_color[1] + noise + random.randint(-variation//3, variation//3)))
            b = max(0, min(255, base_color[2] + noise + random.randint(-variation//3, variation//3)))
            pixels[x, y] = (r, g, b)

    return img

def create_gradient_texture(size, base_color, gradient_type="radial"):
    """Create a full texture with gradient effect"""
    img = Image.new('RGB', (size, size))
    pixels = img.load()

    center_x, center_y = size // 2, size // 2
    max_distance = math.sqrt(center_x**2 + center_y**2)

    highlight_color = tuple(min(255, c + 60) for c in base_color)
    shadow_color = tuple(max(0, c - 40) for c in base_color)

    for x in range(size):
        for y in range(size):
            if gradient_type == "radial":
                distance = math.sqrt((x - center_x)**2 + (y - center_y)**2)
                ratio = distance / max_distance
            elif gradient_type == "linear":
                ratio = y / size
            else:
                ratio = (x + y) / (2 * size)

            r = int(highlight_color[0] + (shadow_color[0] - highlight_color[0]) * ratio)
            g = int(highlight_color[1] + (shadow_color[1] - highlight_color[1]) * ratio)
            b = int(highlight_color[2] + (shadow_color[2] - highlight_color[2]) * ratio)

            pixels[x, y] = (r, g, b)

    return img

def add_noise_texture(img, intensity=0.1):
    """Add subtle noise for texture variation"""
    size = img.size[0]
    pixels = img.load()

    for x in range(size):
        for y in range(size):
            r, g, b = pixels[x, y]

            noise = random.randint(-int(255 * intensity), int(255 * intensity))
            r = max(0, min(255, r + noise))
            g = max(0, min(255, g + noise))
            b = max(0, min(255, b + noise))

            pixels[x, y] = (r, g, b)

    return img

def create_strawberry_texture(size, base_color):
    """Create strawberry texture with realistic seeds and subtle bumpiness"""
    img = create_gradient_texture(size, base_color, "radial")
    draw = ImageDraw.Draw(img)

    seed_color = (90, 45, 20)  # Dark brown seeds
    seed_highlight = (160, 90, 40)

    # Seeds with irregular placement and varying sizes
    num_seeds = size // 10
    for _ in range(num_seeds * num_seeds // 2):
        seed_x = random.randint(0, size)
        seed_y = random.randint(0, size)

        # Avoid center highlight area
        center = size // 2
        dist = math.sqrt((seed_x - center) ** 2 + (seed_y - center) ** 2)
        if dist < size * 0.25:
            continue

        # Vary seed size and orientation
        w = random.randint(2, 4)
        h = w + random.randint(0, 2)
        angle = random.uniform(-20, 20)

        # Draw seed oval rotated (simulate by ellipse with random offset)
        bbox = [seed_x - w, seed_y - h, seed_x + w, seed_y + h]
        draw.ellipse(bbox, fill=seed_color)
        highlight_bbox = [seed_x - w//2, seed_y - h//2 - 1, seed_x + w//2, seed_y + h//2 - 1]
        draw.ellipse(highlight_bbox, fill=seed_highlight)

    # Add subtle bump texture via noise and blur
    img = add_noise_texture(img, intensity=0.06)
    img = img.filter(ImageFilter.GaussianBlur(radius=0.6))

    return img

def create_watermelon_texture(size, base_color):
    """Create watermelon texture with dynamic wavy stripes"""
    img = create_gradient_texture(size, base_color, "linear")
    draw = ImageDraw.Draw(img)

    stripe_color = tuple(max(0, c - 100) for c in base_color)
    stripe_width = size // 20

    for i in range(-stripe_width * 2, size + stripe_width * 2, stripe_width * 3):
        for y in range(size):
            # Sinusoidal wave with noise for natural stripes
            wave_offset = int(12 * math.sin((y * 4 * math.pi) / size + i) + random.randint(-2, 2))
            stripe_start = i + wave_offset
            stripe_end = stripe_start + stripe_width

            if 0 <= stripe_start < size and 0 <= stripe_end < size:
                draw.line([(stripe_start, y), (stripe_end, y)], fill=stripe_color)

    # Add light green subtle patches (rind texture)
    patch_color = tuple(min(255, c + 30) for c in base_color)
    for _ in range(size // 50):
        px = random.randint(0, size)
        py = random.randint(0, size)
        radius = random.randint(size // 30, size // 15)
        draw.ellipse([px-radius, py-radius, px+radius, py+radius], fill=patch_color + (50,))

    # Slight blur for softness
    img = img.filter(ImageFilter.GaussianBlur(radius=0.8))

    return img

def create_pineapple_texture(size, base_color):
    """Create pineapple texture with detailed diamond pattern and bumps"""
    img = create_gradient_texture(size, base_color, "radial")
    draw = ImageDraw.Draw(img)

    pattern_color = tuple(max(0, c - 50) for c in base_color)
    diamond_fill = tuple(max(0, c - 30) for c in base_color)
    diamond_shadow = tuple(max(0, c - 90) for c in base_color)

    spacing = size // 14

    for x in range(-spacing, size + spacing, spacing):
        for y in range(-spacing, size + spacing, spacing):
            # Diamond vertices with slight jitter for organic feel
            points = [
                (x + spacing//2 + random.randint(-2,2), y + random.randint(-2,2)),
                (x + spacing + random.randint(-2,2), y + spacing//2 + random.randint(-2,2)),
                (x + spacing//2 + random.randint(-2,2), y + spacing + random.randint(-2,2)),
                (x + random.randint(-2,2), y + spacing//2 + random.randint(-2,2))
            ]
            draw.polygon(points, outline=pattern_color, width=2)
            # Fill diamond center with a darker tone
            center_points = [
                (points[0][0], (points[0][1] + points[3][1]) // 2),
                ((points[0][0] + points[1][0]) // 2, points[1][1]),
                (points[2][0], (points[2][1] + points[1][1]) // 2),
                ((points[3][0] + points[2][0]) // 2, points[3][1])
            ]
            draw.polygon(center_points, fill=diamond_fill)

    # Add small circular bumps for pineapple texture
    bump_color = tuple(max(0, c - 70) for c in base_color)
    for _ in range(size // 30):
        bx = random.randint(0, size)
        by = random.randint(0, size)
        br = random.randint(2, 4)
        draw.ellipse([bx-br, by-br, bx+br, by+br], outline=bump_color, width=1)

    img = add_noise_texture(img, intensity=0.05)
    img = img.filter(ImageFilter.GaussianBlur(radius=0.4))

    return img

def create_orange_texture(size, base_color):
    """Create orange texture with realistic peel dimples and bumpiness"""
    img = create_gradient_texture(size, base_color, "radial")
    draw = ImageDraw.Draw(img)

    # Noise base for rough peel
    img = add_noise_texture(img, intensity=0.18)

    dimple_color = tuple(max(0, c - 60) for c in base_color)
    dimple_highlight = tuple(min(255, c + 50) for c in base_color)

    dimple_spacing = size // 20
    for x in range(dimple_spacing//2, size, dimple_spacing):
        for y in range(dimple_spacing//2, size, dimple_spacing):
            offset_x = random.randint(-dimple_spacing//4, dimple_spacing//4)
            offset_y = random.randint(-dimple_spacing//4, dimple_spacing//4)
            dimple_x = x + offset_x
            dimple_y = y + offset_y
            radius = random.randint(3, 5)

            # Draw dimples with highlights (slightly elliptical)
            draw.ellipse([dimple_x-radius, dimple_y-radius//2, dimple_x+radius, dimple_y+radius//2], fill=dimple_color)
            draw.ellipse([dimple_x-radius//2, dimple_y-radius//3, dimple_x+radius//2, dimple_y+radius//6], fill=dimple_highlight)

    # Add slight blur to blend dimples smoothly
    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))

    return img

def create_grape_texture(size, base_color):
    """Create grape texture with realistic bubble and shine effects"""
    img = create_gradient_texture(size, base_color, "radial")
    draw = ImageDraw.Draw(img)

    highlight_color = tuple(min(255, c + 60) for c in base_color)
    shadow_color = tuple(max(0, c - 50) for c in base_color)

    bubble_spacing = size // 20
    for x in range(bubble_spacing//2, size, bubble_spacing):
        for y in range(bubble_spacing//2, size, bubble_spacing):
            offset_x = random.randint(-bubble_spacing//3, bubble_spacing//3)
            offset_y = random.randint(-bubble_spacing//3, bubble_spacing//3)
            bubble_x = x + offset_x
            bubble_y = y + offset_y
            radius = random.randint(5, 8)

            # Bubble base (slightly transparent shadows)
            draw.ellipse([bubble_x-radius, bubble_y-radius, bubble_x+radius, bubble_y+radius], fill=shadow_color)

            # Shiny highlight (small bright ellipse)
            hl_w = radius // 2
            hl_h = radius // 3
            draw.ellipse([bubble_x - hl_w, bubble_y - hl_h*2, bubble_x + hl_w, bubble_y - hl_h], fill=highlight_color)

    # Add gloss and subtle noise
    img = add_noise_texture(img, intensity=0.05)
    img = img.filter(ImageFilter.GaussianBlur(radius=0.3))

    return img

def create_fruit_texture(color, size=512, fruit_type="default"):
    """Create full square fruit texture"""
    random.seed()  # Use system time for better randomness

    if fruit_type == "strawberry":
        return create_strawberry_texture(size, color)
    elif fruit_type == "watermelon":
        return create_watermelon_texture(size, color)
    elif fruit_type == "pineapple":
        return create_pineapple_texture(size, color)
    elif fruit_type in ["dekopon", "persimmon"]:
        return create_orange_texture(size, color)
    elif fruit_type == "grape":
        return create_grape_texture(size, color)
    else:
        img = create_gradient_texture(size, color, "radial")
        return add_noise_texture(img, intensity=0.08)

def create_all_fruit_textures():
    output_dir = "resources/textures/fruits"
    os.makedirs(output_dir, exist_ok=True)

    fruit_data = {
        'cherry': ((139, 0, 0), "default"),
        'strawberry': ((255, 60, 60), "strawberry"),
        'grape': ((128, 0, 128), "grape"),
        'dekopon': ((255, 140, 0), "dekopon"),
        'persimmon': ((255, 69, 0), "persimmon"),
        'apple': ((220, 20, 60), "default"),
        'pear': ((173, 255, 47), "default"),
        'peach': ((255, 218, 185), "default"),
        'pineapple': ((255, 215, 0), "pineapple"),
        'melon': ((144, 238, 144), "default"),
        'watermelon': ((0, 100, 0), "watermelon"),
    }

    print("Generating full square fruit textures...")

    for fruit_name, (color, fruit_type) in fruit_data.items():
        filename = f"{fruit_name}.png"
        filepath = os.path.join(output_dir, filename)

        texture = create_fruit_texture(color, size=512, fruit_type=fruit_type)
        texture.save(filepath)
        print(f"✓ Created {filepath}")

    print(f"\n🎉 Full square textures created successfully!")
    print(f"📁 Output directory: {output_dir}")
    print(f"📊 Total files: {len(fruit_data)}")

# The rest of your size variants and main code remains the same

if __name__ == "__main__":
    print("🍎 Full Square Fruit Texture Generator")
    print("=" * 40)
    print("Creates complete square textures (not circles)")
    print("Perfect for UV mapping on 3D spheres")
    print()

    print("Choose option:")
    print("1. Standard textures (512x512)")
    print("2. Multiple size variants")
    print("3. Both")

    choice = input("Enter 1, 2, or 3: ")

    try:
        if choice == "1":
            create_all_fruit_textures()
        elif choice == "2":
            create_size_variants()
        elif choice == "3":
            create_all_fruit_textures()
            create_size_variants()
        else:
            print("Invalid choice. Creating standard textures...")
            create_all_fruit_textures()

    except ImportError:
        print("❌ Error: PIL (Pillow) library not found!")
        print("Install it with: pip install Pillow")
    except Exception as e:
        print(f"❌ Error: {e}")

    input("\nPress Enter to exit...")
