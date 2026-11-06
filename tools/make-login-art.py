#!/usr/bin/env python3
"""Draw the login screen's Saturn, in plain ASCII with colour, into board/astro/issue.in.

Usage: make-login-art.py OUTPUT

Works out the planet and its tilted rings for each character cell, so the rings
pass behind the planet at the top and in front of it at the bottom. Avoid
backslashes in the output, as getty treats them as escape codes in /etc/issue.
"""

import math
import random
import sys

COLS = 62
ROWS = 11
CENTER_X = COLS / 2.0
CENTER_Y = (ROWS - 1) / 2.0
CELL_ASPECT = 2.1  # A character cell is about twice as tall as it is wide

PLANET_RADIUS = 9.5
RING_TILT = math.radians(-12.0)
RING_FLATTEN = 0.21
RING_BANDS = ((13.5, 18.5), (20.0, 27.0))

PLANET_RAMP = ".:-=+*#%"
PLANET_COLOURS = (94, 94, 136, 136, 179, 179, 222, 223)
RING_CHARS = ("~", "=")
RING_COLOURS = (180, 223)
STARS = ((".", 60), (".", 67), (".", 103), ("+", 103), ("*", 146), ("*", 223))
STAR_CHANCE = 0.05


def cell(col, row):
    x = col + 0.5 - CENTER_X
    y = (row - CENTER_Y) * CELL_ASPECT
    u = x * math.cos(RING_TILT) - y * math.sin(RING_TILT)
    v = x * math.sin(RING_TILT) + y * math.cos(RING_TILT)
    ring_distance = math.hypot(u, v / RING_FLATTEN)

    ring = None
    for band, (inner, outer) in enumerate(RING_BANDS):
        if inner <= ring_distance < outer:
            ring = band

    in_front = v > 0.0
    planet = (u / PLANET_RADIUS) ** 2 + (v / (PLANET_RADIUS * 0.93)) ** 2

    if ring is not None and (in_front or planet > 1.0):
        return RING_CHARS[ring], RING_COLOURS[ring]

    if planet <= 1.0:
        z = math.sqrt(1.0 - planet)
        # Lit from the upper left, with faint cloud bands along the equator
        light = max(0.0, -0.52 * u / PLANET_RADIUS - 0.42 * v / PLANET_RADIUS + 0.74 * z)
        light *= 0.88 + 0.12 * math.sin(v * 1.5)
        index = min(int(light * len(PLANET_RAMP)), len(PLANET_RAMP) - 1)
        return PLANET_RAMP[index], PLANET_COLOURS[index]

    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    random.seed(20261106)
    lines = ["\033[?25h"]  # The kernel command line hides the cursor for the app
    for row in range(ROWS):
        text = "    "
        current = None
        for col in range(COLS):
            drawn = cell(col, row)
            if drawn is None:
                if random.random() < STAR_CHANCE:
                    drawn = random.choice(STARS)
                else:
                    text += " "
                    continue
            char, colour = drawn
            if colour != current:
                text += f"\033[38;5;{colour}m"
                current = colour
            text += char
        lines.append(text.rstrip() + "\033[0m")
    lines.append("")
    lines.append("    \033[38;5;223mAstro OS @VERSION@\033[0m")
    lines.append("")

    with open(sys.argv[1], "w") as output:
        output.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
