from PIL import Image
import os
import subprocess
import shutil

gif_path = r"src/xrlp9446d86g1.gif"
resized_gif_path = r"src/gif_resized.gif"
output_header = r"src/gif_data.h"

TW, TH = 240, 320

# Check if ffmpeg is available for better quality
has_ffmpeg = shutil.which("ffmpeg") is not None

if has_ffmpeg:
    print("Using ffmpeg for high-quality resize...")
    # ffmpeg preserves GIF palettes much better than Pillow
    # Scale to cover 240x320, then crop center
    cmd = [
        "ffmpeg", "-y", "-i", gif_path,
        "-vf", f"scale={TW}:{TH}:force_original_aspect_ratio=increase,crop={TW}:{TH},split[s0][s1];[s0]palettegen=max_colors=128[p];[s1][p]paletteuse=dither=bayer",
        "-r", "10",
        "-loop", "0",
        resized_gif_path
    ]
    subprocess.run(cmd, check=True, capture_output=True)
else:
    print("ffmpeg not found, using Pillow...")
    g = Image.open(gif_path)
    scale = max(TW / g.size[0], TH / g.size[1])
    new_w = int(g.size[0] * scale)
    new_h = int(g.size[1] * scale)
    
    frames = []
    durations = []
    for i in range(g.n_frames):
        g.seek(i)
        dur = g.info.get("duration", 100)
        # Keep as P (palette) mode, don't convert to RGBA
        frame = g.copy()
        frame = frame.resize((new_w, new_h), Image.LANCZOS)
        left = (new_w - TW) // 2
        top = (new_h - TH) // 2
        frame = frame.crop((left, top, left + TW, top + TH))
        # Quantize back to 256 colors to keep as proper GIF
        if frame.mode != "P":
            frame = frame.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
        frames.append(frame)
        durations.append(dur)

    frames[0].save(resized_gif_path, save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True)

resized_size = os.path.getsize(resized_gif_path)
print(f"Resized GIF: {resized_size} bytes ({resized_size/1024:.1f} KB)")

# If still too large, skip frames
if resized_size > 700000:
    print(f"Too large ({resized_size/1024:.0f}KB), reducing frames...")
    g = Image.open(resized_gif_path)
    skip = max(2, g.n_frames // 15)  # aim for ~15 frames
    frames = []
    durations = []
    for i in range(0, g.n_frames, skip):
        g.seek(i)
        frames.append(g.copy())
        durations.append(g.info.get("duration", 100) * skip)
    
    frames[0].save(resized_gif_path, save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True)
    resized_size = os.path.getsize(resized_gif_path)
    print(f"After frame skip: {resized_size} bytes ({resized_size/1024:.1f} KB)")

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

print(f"Done! Header: {os.path.getsize(output_header)/1024:.1f} KB, GIF data: {len(data)/1024:.1f} KB")


