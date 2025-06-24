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
    
    # Fill entire texture with varied colors
    for x in range(size):
        for y in range(size):
            # Add random variation to base color
            r = max(0, min(255, base_color[0] + random.randint(-variation, variation)))
            g = max(0, min(255, base_color[1] + random.randint(-variation, variation)))
            b = max(0, min(255, base_color[2] + random.randint(-variation, variation)))
            pixels[x, y] = (r, g, b)
    
    return img

def create_gradient_texture(size, base_color, gradient_type="radial"):
    """Create a full texture with gradient effect"""
    img = Image.new('RGB', (size, size))
    pixels = img.load()
    
    center_x, center_y = size // 2, size // 2
    max_distance = math.sqrt(center_x**2 + center_y**2)
    
    # Calculate highlight and shadow colors
    highlight_color = tuple(min(255, c + 60) for c in base_color)
    shadow_color = tuple(max(0, c - 40) for c in base_color)
    
    for x in range(size):
        for y in range(size):
            if gradient_type == "radial":
                # Radial gradient from center
                distance = math.sqrt((x - center_x)**2 + (y - center_y)**2)
                ratio = distance / max_distance
            elif gradient_type == "linear":
                # Linear gradient from top to bottom
                ratio = y / size
            else:
                # Diagonal gradient
                ratio = (x + y) / (2 * size)
            
            # Interpolate between highlight and shadow
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
            
            # Add noise
            noise = random.randint(-int(255 * intensity), int(255 * intensity))
            r = max(0, min(255, r + noise))
            g = max(0, min(255, g + noise))
            b = max(0, min(255, b + noise))
            
            pixels[x, y] = (r, g, b)
    
    return img

def create_strawberry_texture(size, base_color):
    """Create strawberry texture with seeds pattern"""
    img = create_gradient_texture(size, base_color, "radial")
    draw = ImageDraw.Draw(img)
    
    # Add seeds pattern across entire texture
    seed_color = (139, 69, 19)  # Brown
    seed_highlight = (160, 90, 40)
    
    # Create regular seed pattern
    seed_spacing = size // 20
    for x in range(seed_spacing, size - seed_spacing, seed_spacing):
        for y in range(seed_spacing, size - seed_spacing, seed_spacing):
            # Add some randomness to seed positions
            offset_x = random.randint(-seed_spacing//4, seed_spacing//4)
            offset_y = random.randint(-seed_spacing//4, seed_spacing//4)
            
            seed_x = x + offset_x
            seed_y = y + offset_y
            
            # Draw seed (small oval)
            draw.ellipse([seed_x-2, seed_y-3, seed_x+2, seed_y+1], fill=seed_color)
            # Add highlight to seed
            draw.ellipse([seed_x-1, seed_y-2, seed_x+1, seed_y-1], fill=seed_highlight)
    
    return img

def create_watermelon_texture(size, base_color):
    """Create watermelon texture with stripe pattern"""
    img = create_gradient_texture(size, base_color, "linear")
    draw = ImageDraw.Draw(img)
    
    # Add dark green stripes
    stripe_color = tuple(max(0, c - 80) for c in base_color)
    stripe_width = size // 15
    
    for i in range(0, size, stripe_width * 2):
        # Create wavy stripes
        for y in range(size):
            wave_offset = int(8 * math.sin(y * 2 * math.pi / size))
            stripe_start = i + wave_offset
            stripe_end = stripe_start + stripe_width
            
            if stripe_start >= 0 and stripe_end < size:
                draw.rectangle([stripe_start, y, stripe_end, y+1], fill=stripe_color)
    
    return img

def create_pineapple_texture(size, base_color):
    """Create pineapple texture with diamond pattern"""
    img = create_gradient_texture(size, base_color, "radial")
    draw = ImageDraw.Draw(img)
    
    # Add diamond crosshatch pattern
    pattern_color = tuple(max(0, c - 40) for c in base_color)
    darker_color = tuple(max(0, c - 80) for c in base_color)
    
    spacing = size // 12
    
    # Draw diamond grid pattern
    for x in range(0, size, spacing):
        for y in range(0, size, spacing):
            # Draw diamond outline
            points = [
                (x + spacing//2, y),
                (x + spacing, y + spacing//2),
                (x + spacing//2, y + spacing),
                (x, y + spacing//2)
            ]
            draw.polygon(points, outline=pattern_color, width=2)
            
            # Fill center with darker color
            center_points = [
                (x + spacing//2, y + spacing//4),
                (x + 3*spacing//4, y + spacing//2),
                (x + spacing//2, y + 3*spacing//4),
                (x + spacing//4, y + spacing//2)
            ]
            draw.polygon(center_points, fill=darker_color)
    
    return img

def create_orange_texture(size, base_color):
    """Create orange texture with peel dimples"""
    img = create_gradient_texture(size, base_color, "radial")
    
    # Add noise for orange peel texture
    img = add_noise_texture(img, intensity=0.15)
    
    draw = ImageDraw.Draw(img)
    
    # Add dimples pattern
    dimple_color = tuple(max(0, c - 50) for c in base_color)
    dimple_highlight = tuple(min(255, c + 30) for c in base_color)
    
    dimple_spacing = size // 25
    for x in range(dimple_spacing, size - dimple_spacing, dimple_spacing):
        for y in range(dimple_spacing, size - dimple_spacing, dimple_spacing):
            # Add randomness
            offset_x = random.randint(-dimple_spacing//3, dimple_spacing//3)
            offset_y = random.randint(-dimple_spacing//3, dimple_spacing//3)
            
            dimple_x = x + offset_x
            dimple_y = y + offset_y
            
            # Draw dimple (small circle with highlight)
            draw.ellipse([dimple_x-3, dimple_y-3, dimple_x+3, dimple_y+3], fill=dimple_color)
            draw.ellipse([dimple_x-1, dimple_y-2, dimple_x+2, dimple_y+1], fill=dimple_highlight)
    
    return img

def create_grape_texture(size, base_color):
    """Create grape texture with subtle bubble pattern"""
    img = create_gradient_texture(size, base_color, "radial")
    draw = ImageDraw.Draw(img)
    
    # Add subtle circular patterns to simulate grape skin
    highlight_color = tuple(min(255, c + 40) for c in base_color)
    shadow_color = tuple(max(0, c - 30) for c in base_color)
    
    bubble_spacing = size // 30
    for x in range(bubble_spacing, size - bubble_spacing, bubble_spacing):
        for y in range(bubble_spacing, size - bubble_spacing, bubble_spacing):
            offset_x = random.randint(-bubble_spacing//2, bubble_spacing//2)
            offset_y = random.randint(-bubble_spacing//2, bubble_spacing//2)
            
            bubble_x = x + offset_x
            bubble_y = y + offset_y
            
            # Small bubble effect
            draw.ellipse([bubble_x-4, bubble_y-4, bubble_x+4, bubble_y+4], fill=shadow_color)
            draw.ellipse([bubble_x-2, bubble_y-3, bubble_x+2, bubble_y+1], fill=highlight_color)
    
    return img

def create_fruit_texture(color, size=512, fruit_type="default"):
    """Create full square fruit texture"""
    random.seed(42)  # For consistent results
    
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
        # Default smooth gradient texture
        img = create_gradient_texture(size, color, "radial")
        return add_noise_texture(img, intensity=0.08)

def create_all_fruit_textures():
    """Create all fruit textures as full square textures"""
    
    output_dir = "resources/textures/fruits"
    os.makedirs(output_dir, exist_ok=True)
    
    # Fruit colors and types
    fruit_data = {
        'cherry': ((139, 0, 0), "default"),           # Dark red
        'strawberry': ((255, 60, 60), "strawberry"),  # Bright red with seeds
        'grape': ((128, 0, 128), "grape"),            # Purple with bubble texture
        'dekopon': ((255, 140, 0), "dekopon"),        # Orange with dimples
        'persimmon': ((255, 69, 0), "persimmon"),     # Orange-red with texture
        'apple': ((220, 20, 60), "default"),          # Crimson red
        'pear': ((173, 255, 47), "default"),          # Yellow-green
        'peach': ((255, 218, 185), "default"),        # Peach color
        'pineapple': ((255, 215, 0), "pineapple"),    # Golden with pattern
        'melon': ((144, 238, 144), "default"),        # Light green
        'watermelon': ((0, 100, 0), "watermelon"),    # Dark green with stripes
    }
    
    print("Generating full square fruit textures...")
    
    for fruit_name, (color, fruit_type) in fruit_data.items():
        filename = f"{fruit_name}.png"
        filepath = os.path.join(output_dir, filename)
        
        # Create texture
        texture = create_fruit_texture(color, size=512, fruit_type=fruit_type)
        
        # Save texture
        texture.save(filepath)
        print(f"✓ Created {filepath}")
    
    print(f"\n🎉 Full square textures created successfully!")
    print(f"📁 Output directory: {output_dir}")
    print(f"📊 Total files: {len(fruit_data)}")

def create_size_variants():
    """Create multiple sizes for performance optimization"""
    
    sizes = [64, 128, 256, 512, 1024]
    base_dir = "resources/textures/fruits"
    
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
    
    print("Creating size variants...")
    
    for size in sizes:
        size_dir = os.path.join(base_dir, f"{size}x{size}")
        os.makedirs(size_dir, exist_ok=True)
        
        for fruit_name, (color, fruit_type) in fruit_data.items():
            filename = f"{fruit_name}.png"
            filepath = os.path.join(size_dir, filename)
            
            texture = create_fruit_texture(color, size=size, fruit_type=fruit_type)
            texture.save(filepath)
            
        print(f"✓ Created {size}x{size} variants")
    
    print("🎉 All size variants created!")

def create_seamless_texture(size, base_color, pattern_func):
    """Create seamless tileable texture (advanced feature)"""
    # Create texture that tiles seamlessly
    img = Image.new('RGB', (size, size))
    
    # This would require more complex math to ensure seamless tiling
    # For now, just create regular texture
    return create_gradient_texture(size, base_color, "radial")

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