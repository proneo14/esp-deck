from PIL import Image
import os

gif_path = r"src/xrlp9446d86g1.gif"
resized_gif_path = r"src/gif_resized.gif"
output_header = r"src/gif_data.h"

# Target display size
TW, TH = 240, 320

g = Image.open(gif_path)
print(f"Original GIF: {g.size[0]}x{g.size[1]}, {g.n_frames} frames")

# Scale and crop to 240x320
scale = max(TW / g.size[0], TH / g.size[1])
new_w = int(g.size[0] * scale)
new_h = int(g.size[1] * scale)

# Use every 2nd frame to reduce size
skip = 2
frames = []
durations = []

for i in range(0, g.n_frames, skip):
    g.seek(i)
    dur = g.info.get("duration", 100)
    frame = g.convert("RGBA")
    frame = frame.resize((new_w, new_h), Image.LANCZOS)
    left = (new_w - TW) // 2
    top = (new_h - TH) // 2
    frame = frame.crop((left, top, left + TW, top + TH))
    frames.append(frame)
    durations.append(dur * skip)

print(f"Resized to {TW}x{TH}, {len(frames)} frames")

# Save as resized GIF
frames[0].save(resized_gif_path, save_all=True, append_images=frames[1:],
               duration=durations, loop=0, optimize=True)

resized_size = os.path.getsize(resized_gif_path)
print(f"Resized GIF: {resized_size} bytes ({resized_size/1024:.1f} KB)")

if resized_size > 2 * 1024 * 1024:
    print("Still too large! Trying with more frame skipping...")
    skip = 4
    frames = []
    durations = []
    g.seek(0)
    for i in range(0, g.n_frames, skip):
        g.seek(i)
        dur = g.info.get("duration", 100)
        frame = g.convert("RGBA")
        frame = frame.resize((new_w, new_h), Image.LANCZOS)
        left = (new_w - TW) // 2
        top = (new_h - TH) // 2
        frame = frame.crop((left, top, left + TW, top + TH))
        frames.append(frame)
        durations.append(dur * skip)
    
    frames[0].save(resized_gif_path, save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True)
    resized_size = os.path.getsize(resized_gif_path)
    print(f"Resized GIF (skip {skip}): {resized_size} bytes ({resized_size/1024:.1f} KB)")

# Convert to C array
with open(resized_gif_path, "rb") as f:
    data = f.read()

with open(output_header, "w") as f:
    f.write("#pragma once\n")
    f.write("#include <pgmspace.h>\n\n")
    f.write(f"const uint8_t gif_data[] PROGMEM = {{\n")
    for i, b in enumerate(data):
        f.write(f"0x{b:02x},")
        if (i + 1) % 16 == 0:
            f.write("\n")
    f.write("};\n\n")
    f.write(f"const size_t gif_data_len = {len(data)};\n")

print(f"Done! Header written to {output_header}")
print(f"Header size: {os.path.getsize(output_header)/1024:.1f} KB")


