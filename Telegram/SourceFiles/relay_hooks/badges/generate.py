import os
import math
from PIL import Image, ImageDraw

OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))
ARTIFACT_DIR = r"C:\Users\creamblyy\.gemini\antigravity\brain\98035f39-dbee-4899-9745-f97d4357ae5e\badges"
os.makedirs(ARTIFACT_DIR, exist_ok=True)

# Colors
BG_COLOR = "#656c73"      # Slate rosette from user image
DARK_BG = "#2b3035"       # Telegram dark background for preview cards
WHITE = "#FFFFFF"

ROSETTE_PATH = (
    "M11.2227 4.03192C11.6432 3.58195 12.3568 3.58195 12.7773 4.03192"
    "L14.1956 5.54939C14.4054 5.77382 14.7019 5.89664 15.0089 5.88626"
    "L17.0848 5.81611C17.7003 5.7953 18.2049 6.29988 18.1841 6.91544"
    "L18.1139 8.99131C18.1036 9.29832 18.2264 9.59484 18.4508 9.80459"
    "L19.9683 11.2229C20.4182 11.6434 20.4182 12.357 19.9683 12.7775"
    "L18.4508 14.1958C18.2264 14.4056 18.1036 14.7021 18.1139 15.0091"
    "L18.1841 17.085C18.2049 17.7005 17.7003 18.2051 17.0848 18.1843"
    "L15.0089 18.1141C14.7019 18.1038 14.4054 18.2266 14.1956 18.451"
    "L12.7773 19.9685C12.3568 20.4184 11.6432 20.4184 11.2227 19.9685"
    "L9.80439 18.451C9.59464 18.2266 9.29813 18.1038 8.99111 18.1141"
    "L6.91524 18.1843C6.29969 18.2051 5.79511 17.7005 5.81591 17.085"
    "L5.88606 15.0091C5.89644 14.7021 5.77362 14.4056 5.54919 14.1958"
    "L4.03172 12.7775C3.58175 12.357 3.58175 11.6434 4.03172 11.2229"
    "L5.54919 9.80459C5.77362 9.59484 5.89644 9.29832 5.88606 8.99131"
    "L5.81591 6.91544C5.79511 6.29988 6.29968 5.7953 6.91524 5.81611"
    "L8.99111 5.88626C9.29813 5.89664 9.59464 5.77382 9.80439 5.54939"
    "L11.2227 4.03192Z"
)

BADGES = {
    "badge_desktop": {
        "title": "Zen Desktop (Core Client)",
        "desc": "Значок официального десктопного клиента",
        "svg_inner": """
        <!-- Monitor screen -->
        <rect x="7.4" y="7.5" width="9.2" height="6.6" rx="1.2" fill="none" stroke="#FFFFFF" stroke-width="1.3" stroke-linejoin="round"/>
        <!-- Monitor stand -->
        <path d="M12 14.1v2.0" stroke="#FFFFFF" stroke-width="1.3" stroke-linecap="round"/>
        <!-- Monitor base foot -->
        <path d="M9.8 16.1h4.4" stroke="#FFFFFF" stroke-width="1.3" stroke-linecap="round"/>
        """
    },
    "badge_dev": {
        "title": "Zen Developer (Maintainer)",
        "desc": "Значок разработчика ядра и плагинов RelayHooks",
        "svg_inner": """
        <!-- Command Prompt Chevron > -->
        <path d="M8.2 9.0L11.3 12.0L8.2 15.0" fill="none" stroke="#FFFFFF" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/>
        <!-- Underscore Cursor _ -->
        <path d="M12.8 15.0h3.0" stroke="#FFFFFF" stroke-width="1.4" stroke-linecap="round"/>
        """
    },
    "badge_staff": {
        "title": "Zen Staff (Verified / Team)",
        "desc": "Значок верифицированного сотрудника и команды",
        "svg_inner": """
        <!-- Sharp Lightning Bolt -->
        <path d="M12.8 6.5L8.5 12.4h3.3L11.0 17.5l5.0-6.4h-3.4l1.2-4.6z" fill="#FFFFFF" stroke="#FFFFFF" stroke-width="0.5" stroke-linejoin="round"/>
        """
    },
    "badge_supporter": {
        "title": "Zen Supporter (VIP / Sponsor)",
        "desc": "Значок спонсора и премиум-пользователя",
        "svg_inner": """
        <!-- 4-point Swiss Spark / Star -->
        <path d="M12 6.6c.4 3.0 2.4 5.0 5.4 5.4-3.0.4-5.0 2.4-5.4 5.4-.4-3.0-2.4-5.0-5.4-5.4 3.0-.4 5.0-2.4 5.4-5.4z" fill="#FFFFFF"/>
        """
    }
}

def generate_svgs():
    for name, data in BADGES.items():
        svg_content = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg width="24" height="24" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
    <!-- 8-lobe scalloped rosette background -->
    <path d="{ROSETTE_PATH}" fill="{BG_COLOR}"/>
    {data["svg_inner"]}
</svg>'''
        # Write to local badges folder
        local_path = os.path.join(OUTPUT_DIR, f"{name}.svg")
        with open(local_path, "w", encoding="utf-8") as f:
            f.write(svg_content)
        # Write to artifact folder
        art_path = os.path.join(ARTIFACT_DIR, f"{name}.svg")
        with open(art_path, "w", encoding="utf-8") as f:
            f.write(svg_content)
        print(f"Generated {name}.svg")

def draw_highres_pngs():
    # Render 512x512 with PIL supersampling down to 256x256
    S = 1024
    scale = S / 24.0

    def pt(x, y):
        return (x * scale, y * scale)

    # Function to create smooth 8-lobe rosette polygon
    # A 8-lobe rosette is union of two rounded squares rotated 45 deg,
    # or r(theta) = R0 + A * cos(8*theta)
    # Let's generate a dense polygon
    def get_rosette_poly():
        points = []
        cx, cy = S / 2, S / 2
        # Radius parameters tuned to match exact Telegram badge geometry:
        # Min radius ~ 7.4 in 24 coord, Max radius ~ 9.5
        for i in range(720):
            th = 2 * math.pi * i / 720
            # 8 lobes
            r = (8.45 + 1.25 * math.cos(8 * th)) * scale
            points.append((cx + r * math.cos(th), cy + r * math.sin(th)))
        return points

    poly = get_rosette_poly()

    for name in BADGES.keys():
        im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        draw = ImageDraw.Draw(im)

        # Draw rosette
        draw.polygon(poly, fill=(101, 108, 115, 255))

        # Inner icons
        if name == "badge_desktop":
            # Monitor
            rx0, ry0 = pt(7.4, 7.5)
            rx1, ry1 = pt(16.6, 14.1)
            lw = int(1.3 * scale)
            corner = int(1.2 * scale)
            draw.rounded_rectangle([rx0, ry0, rx1, ry1], radius=corner, outline=(255, 255, 255, 255), width=lw)
            # Stand
            sx, sy0 = pt(12.0, 14.1)
            _, sy1 = pt(12.0, 16.1)
            draw.line([(sx, sy0), (sx, sy1)], fill=(255, 255, 255, 255), width=lw)
            # Foot
            fx0, fy = pt(9.8, 16.1)
            fx1, _ = pt(14.2, 16.1)
            draw.line([(fx0, fy), (fx1, fy)], fill=(255, 255, 255, 255), width=lw)

        elif name == "badge_dev":
            # Chevron >
            lw = int(1.4 * scale)
            p1 = pt(8.2, 9.0)
            p2 = pt(11.3, 12.0)
            p3 = pt(8.2, 15.0)
            draw.line([p1, p2, p3], fill=(255, 255, 255, 255), width=lw, joint="round")
            # Underscore _
            u0 = pt(12.8, 15.0)
            u1 = pt(15.8, 15.0)
            draw.line([u0, u1], fill=(255, 255, 255, 255), width=lw)

        elif name == "badge_staff":
            # Lightning bolt
            pts = [
                pt(12.8, 6.5),
                pt(8.5, 12.4),
                pt(11.8, 12.4),
                pt(11.0, 17.5),
                pt(16.0, 11.1),
                pt(12.6, 11.1),
            ]
            draw.polygon(pts, fill=(255, 255, 255, 255))

        elif name == "badge_supporter":
            # 4-point star
            cx, cy = pt(12.0, 12.0)
            r_out = 5.4 * scale
            pts = []
            for deg in range(360):
                rad = math.radians(deg)
                # Astroid/star curve
                cost = math.cos(rad)
                sint = math.sin(rad)
                # Superellipse / star
                r = r_out / ((abs(cost)**0.7 + abs(sint)**0.7)**(1/0.7))
                pts.append((cx + r * cost, cy + r * sint))
            draw.polygon(pts, fill=(255, 255, 255, 255))

        # Downsample to 256x256 and 64x64
        im256 = im.resize((256, 256), Image.Resampling.LANCZOS)
        im64 = im.resize((64, 64), Image.Resampling.LANCZOS)

        for d in [OUTPUT_DIR, ARTIFACT_DIR]:
            im256.save(os.path.join(d, f"{name}.png"))
            im64.save(os.path.join(d, f"{name}_small.png"))

        # Also create a composite preview on dark background
        print(f"Generated PNGs for {name}")

    # Create composite preview card
    card = Image.new("RGBA", (800, 260), (43, 48, 53, 255))
    card_draw = ImageDraw.Draw(card)
    names = list(BADGES.keys())
    for idx, name in enumerate(names):
        icon = Image.open(os.path.join(ARTIFACT_DIR, f"{name}.png")).resize((140, 140), Image.Resampling.LANCZOS)
        x = 40 + idx * 190
        card.paste(icon, (x, 50), icon)
    
    card.save(os.path.join(ARTIFACT_DIR, "badges_showcase.png"))
    card.save(os.path.join(OUTPUT_DIR, "badges_showcase.png"))
    print("Generated badges_showcase.png")

if __name__ == "__main__":
    generate_svgs()
    draw_highres_pngs()
