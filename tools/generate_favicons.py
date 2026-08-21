# Copyright (C) 2026 Antonio Ricciardi
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import os
from PIL import Image, ImageDraw, ImageFilter

def draw_minimalist_favicon():
    size = 512
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    
    # 1. Base Squircle - Deep Slate / Telegram Navy
    # Background color: #0e1726 to #1e293b
    bg = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    bg_draw = ImageDraw.Draw(bg)
    
    margin = 20
    radius = 112
    # Telegram Blue / Dark Navy gradient simulation
    bg_draw.rounded_rectangle([margin, margin, size - margin, size - margin], radius=radius, fill=(14, 23, 38, 255))
    
    # Subtle inner border ring
    bg_draw.rounded_rectangle([margin, margin, size - margin, size - margin], radius=radius, outline=(37, 99, 235, 180), width=8)

    # 2. Sleek Minimalist Telegram Paper Plane & Clipboard Motif
    # Vector points for a super clean paper plane:
    # Nose tip at (380, 130)
    # Left wing tail at (130, 310)
    # Right wing tail at (270, 390)
    # Fold join at (230, 305)

    plane_layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    pdraw = ImageDraw.Draw(plane_layer)

    # Left Wing (Bright White)
    pdraw.polygon([(130, 310), (380, 130), (230, 305)], fill=(255, 255, 255, 255))
    
    # Right Wing (Cyan / Sky Blue highlight)
    pdraw.polygon([(230, 305), (380, 130), (270, 390)], fill=(56, 189, 248, 255))
    
    # Bottom Fold Shadow (Deep Telegram Blue)
    pdraw.polygon([(230, 305), (270, 390), (300, 315)], fill=(14, 87, 217, 255))

    # Composite layers
    final_img = Image.alpha_composite(bg, plane_layer)

    docs_dir = "/home/roothunter/Dev/c2t/docs"
    
    # Export PNGs
    final_img.resize((180, 180), Image.Resampling.LANCZOS).save(os.path.join(docs_dir, "apple-touch-icon.png"), "PNG")
    final_img.resize((32, 32), Image.Resampling.LANCZOS).save(os.path.join(docs_dir, "favicon-32x32.png"), "PNG")
    final_img.resize((16, 16), Image.Resampling.LANCZOS).save(os.path.join(docs_dir, "favicon-16x16.png"), "PNG")

    final_img.save(os.path.join(docs_dir, "favicon.ico"), format="ICO", sizes=[(16, 16), (32, 32), (48, 48), (64, 64)])
    print("Minimalist favicons created successfully.")

if __name__ == "__main__":
    draw_minimalist_favicon()
