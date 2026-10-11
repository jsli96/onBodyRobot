#!/usr/bin/env python3
"""
convert_frames.py — Prepare video frames for ESP32 SPIFFS animation

Usage:
    python convert_frames.py input_video.mp4 output_folder/ [--fps 8] [--size 240] [--max-total-kb 1400]

This script:
  1. Extracts frames from a video at a target FPS
  2. Resizes each frame to fit 240x240 (center-cropped to circle-friendly square)
  3. Saves as optimized PNGs named frame_001.png, frame_002.png, ...
  4. Stops when total file size would exceed SPIFFS capacity (~1.4 MB)

The output folder contents should be uploaded to /anim/ on the ESP32 SPIFFS.

Requirements:
    pip install opencv-python Pillow

Upload frames to SPIFFS using the Arduino ESP32 SPIFFS upload plugin,
or via the Arduino IDE "ESP32 Sketch Data Upload" tool. Place the output
PNGs into a folder called "data/anim/" inside your sketch folder, then
use the upload tool.
"""

import argparse
import os
import sys

def main():
    parser = argparse.ArgumentParser(description="Convert video to PNG frames for ESP32 SPIFFS")
    parser.add_argument("input", default="robot-control/HeartBeating.mp4", help="Input video file (mp4, avi, mov, gif, etc.)")
    parser.add_argument("output", default="robot-control/data/anim", help="Output folder for PNG frames")
    parser.add_argument("--fps", type=int, default=8, help="Target frames per second (default: 8)")
    parser.add_argument("--size", type=int, default=240, help="Output frame size in pixels (default: 240)")
    parser.add_argument("--max-total-kb", type=int, default=1400, help="Max total size in KB (default: 1400, ~SPIFFS limit)")
    args = parser.parse_args()

    try:
        import cv2
        from PIL import Image
    except ImportError:
        print("ERROR: This script requires opencv-python and Pillow.")
        print("Install them with:  pip install opencv-python Pillow")
        sys.exit(1)

    os.makedirs(args.output, exist_ok=True)

    cap = cv2.VideoCapture(args.input)
    if not cap.isOpened():
        print(f"ERROR: Cannot open '{args.input}'")
        sys.exit(1)

    src_fps = cap.get(cv2.CAP_PROP_FPS)
    total_src_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    duration = total_src_frames / src_fps if src_fps > 0 else 0

    print(f"Source: {args.input}")
    print(f"  Resolution: {int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))}x{int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))}")
    print(f"  FPS: {src_fps:.1f}, Frames: {total_src_frames}, Duration: {duration:.1f}s")
    print(f"Target: {args.fps} FPS, {args.size}x{args.size} px, max {args.max_total_kb} KB total")
    print()

    # Calculate which source frames to grab
    frame_interval = src_fps / args.fps if args.fps > 0 else 1
    max_bytes = args.max_total_kb * 1024
    total_bytes = 0
    frame_num = 0
    out_idx = 0

    while True:
        target_frame = int(out_idx * frame_interval)
        cap.set(cv2.CAP_PROP_POS_FRAMES, target_frame)
        ret, frame_bgr = cap.read()
        if not ret:
            break

        # Convert BGR → RGB → PIL
        frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        img = Image.fromarray(frame_rgb)

        # Center crop to square
        w, h = img.size
        side = min(w, h)
        left = (w - side) // 2
        top = (h - side) // 2
        img = img.crop((left, top, left + side, top + side))

        # Resize to target
        img = img.resize((args.size, args.size), Image.LANCZOS)

        # Save with optimization
        frame_num += 1
        out_path = os.path.join(args.output, f"frame_{frame_num:03d}.png")
        img.save(out_path, "PNG", optimize=True)

        fsize = os.path.getsize(out_path)
        total_bytes += fsize

        if total_bytes > max_bytes:
            os.remove(out_path)
            frame_num -= 1
            print(f"  Stopped at frame {frame_num} — would exceed {args.max_total_kb} KB limit")
            break

        print(f"  frame_{frame_num:03d}.png  ({fsize // 1024} KB)  total: {total_bytes // 1024} KB")
        out_idx += 1

    cap.release()
    print(f"\nDone! {frame_num} frames saved to '{args.output}/' ({total_bytes // 1024} KB total)")
    print(f"Estimated playback: {frame_num / args.fps:.1f}s at {args.fps} FPS")
    print(f"\nNext steps:")
    print(f"  1. Copy the frames into your sketch's data/anim/ folder")
    print(f"  2. Upload to SPIFFS using the ESP32 Sketch Data Upload tool")
    print(f"  3. Upload the robot-control sketch")


if __name__ == "__main__":
    main()
