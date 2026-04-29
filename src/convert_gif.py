from PIL import Image
import os
import subprocess
import shutil
import argparse

parser = argparse.ArgumentParser(description="Convert a GIF for ESP display")
parser.add_argument("gif_path", nargs="?", help="Path to the input GIF")
parser.add_argument("-W", "--width", type=int, default=100, help="Target width (default: 100)")
parser.add_argument("-H", "--height", type=int, default=100, help="Target height (default: 100)")
parser.add_argument("-s", "--speed", type=float, default=1.0, help="Speed multiplier (2.0 = twice as fast, 0.5 = half speed)")
parser.add_argument("-f", "--fps", type=int, default=None, help="Force a specific FPS (overrides original timing)")
parser.add_argument("-c", "--colors", type=int, default=256, help="Max colors for palette (default: 256, max: 256)")
parser.add_argument("--max-size", type=int, default=700000, help="Max file size in bytes before frame skipping (default: 700000)")
parser.add_argument("-o", "--output", default=None, help="Output header file path")
args = parser.parse_args()

# Resolve paths relative to the script's directory
script_dir = os.path.dirname(os.path.abspath(__file__))

if args.gif_path:
    gif_path = args.gif_path
else:
    gif_path = input(r"Enter the path to the GIF: ")

resized_gif_path = os.path.join(script_dir, "gif_resized.gif")
output_header = args.output if args.output else os.path.join(script_dir, "gif_data.h")

TW, TH = args.width, args.height
colors = min(args.colors, 256)

# Check if ffmpeg is available for better quality
has_ffmpeg = shutil.which("ffmpeg") is not None

if has_ffmpeg:
    print("Using ffmpeg for high-quality resize...")
    vf = f"scale={TW}:{TH}:force_original_aspect_ratio=increase,crop={TW}:{TH}"
    if colors < 256:
        vf += f",split[s0][s1];[s0]palettegen=max_colors={colors}[p];[s1][p]paletteuse=dither=bayer"

    cmd = [
        "ffmpeg", "-y", "-i", gif_path,
        "-vf", vf,
    ]
    if args.fps:
        cmd += ["-r", str(args.fps)]
    cmd += ["-loop", "0", resized_gif_path]
    subprocess.run(cmd, check=True, capture_output=True)

    # Apply speed multiplier to ffmpeg output if needed
    if args.speed != 1.0:
        g = Image.open(resized_gif_path)
        frames = []
        durations = []
        for i in range(g.n_frames):
            g.seek(i)
            dur = g.info.get("duration", 100)
            frames.append(g.copy())
            durations.append(max(10, int(dur / args.speed)))
        frames[0].save(resized_gif_path, save_all=True, append_images=frames[1:],
                       duration=durations, loop=0, optimize=True)
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
        if args.fps:
            dur = int(1000 / args.fps)
        else:
            dur = max(10, int(dur / args.speed))
        frame = g.copy()
        frame = frame.resize((new_w, new_h), Image.LANCZOS)
        left = (new_w - TW) // 2
        top = (new_h - TH) // 2
        frame = frame.crop((left, top, left + TW, top + TH))
        if frame.mode != "P" and colors < 256:
            frame = frame.quantize(colors=colors, method=Image.Quantize.MEDIANCUT)
        frames.append(frame)
        durations.append(dur)

    frames[0].save(resized_gif_path, save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True)

resized_size = os.path.getsize(resized_gif_path)
print(f"Resized GIF: {resized_size} bytes ({resized_size/1024:.1f} KB)")

# If still too large, skip frames
if resized_size > args.max_size:
    print(f"Too large ({resized_size/1024:.0f}KB), reducing frames...")
    g = Image.open(resized_gif_path)
    skip = max(2, g.n_frames // 15)
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


'''
python src/convert_gif.py my.gif                    # defaults, full colors
python src/convert_gif.py my.gif -s 1.5             # 1.5x speed
python src/convert_gif.py my.gif -f 15              # force 15 FPS
python src/convert_gif.py my.gif -W 80 -H 80 -s 2   # 80x80 at 2x speed
python src/convert_gif.py my.gif -c 64              # reduce to 64 colors (opt-in)
'''