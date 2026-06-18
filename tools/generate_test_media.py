"""Generate deterministic SD-card media for ESP32-CYD-Tester."""

from pathlib import Path
import io
import math
import struct
import wave

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "sdcard"
WIDTH, HEIGHT = 320, 240
FPS, SECONDS = 10, 5


def make_mjpeg() -> None:
    colors = [
        (255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
        (255, 0, 255), (255, 0, 0), (0, 0, 255), (20, 20, 20),
    ]
    with (OUTPUT / "test.mjpeg").open("wb") as stream:
        for frame_number in range(FPS * SECONDS):
            image = Image.new("RGB", (WIDTH, HEIGHT), "black")
            draw = ImageDraw.Draw(image)
            bar_width = WIDTH // len(colors)
            for index, color in enumerate(colors):
                draw.rectangle((index * bar_width, 0, (index + 1) * bar_width, HEIGHT), fill=color)

            x = 20 + int((WIDTH - 40) * frame_number / (FPS * SECONDS - 1))
            y = HEIGHT // 2 + int(55 * math.sin(frame_number * 2 * math.pi / FPS))
            draw.ellipse((x - 18, y - 18, x + 18, y + 18), fill=(255, 128, 0), outline="white", width=3)
            draw.rectangle((0, HEIGHT - 34, WIDTH, HEIGHT), fill="black")
            draw.text((8, HEIGHT - 26), f"CYD MJPEG  {frame_number + 1:02}/{FPS * SECONDS}", fill="white")

            encoded = io.BytesIO()
            image.save(encoded, "JPEG", quality=70, optimize=True)
            stream.write(encoded.getvalue())


def make_wav() -> None:
    sample_rate = 22050
    notes = [(440, 0.55), (554, 0.55), (659, 0.55), (880, 0.8), (659, 0.4), (880, 0.9)]
    samples = []
    for frequency, duration in notes:
        count = int(sample_rate * duration)
        for index in range(count):
            envelope = min(1.0, index / 300, (count - index) / 500)
            value = int(30000 * envelope * math.sin(2 * math.pi * frequency * index / sample_rate))
            samples.append(value)
        samples.extend([0] * int(sample_rate * 0.06))

    with wave.open(str(OUTPUT / "test.wav"), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        output.writeframes(b"".join(struct.pack("<h", sample) for sample in samples))


if __name__ == "__main__":
    OUTPUT.mkdir(exist_ok=True)
    make_mjpeg()
    make_wav()
    print(f"Generated media in {OUTPUT}")

