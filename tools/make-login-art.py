#!/usr/bin/env python3
"""Draw the login screen's Andromeda Galaxy, in plain ASCII with colour, into board/astro/issue.in.

Usage: make-login-art.py OUTPUT

Models a tilted spiral galaxy: a warm central bulge, a disk of blue spiral arms with
pink star-forming regions, and dark lanes of dust. This is an artist's impression
with the right structure and colours, not a map of the real galaxy. Keep backslashes
and percent signs out of the output, as getty treats both as escape codes in /etc/issue.
"""

import math
import random
import sys

COLS = 72
ROWS = 17
CENTER_X = COLS / 2.0
CENTER_Y = ROWS / 2.0
CELL_ASPECT = 2.05  # A character cell is about twice as tall as it is wide
SAMPLES = 3

ANGLE = math.radians(-17.0)  # Of the long axis on the screen
TILT_FLATTEN = 0.31          # The disk is seen from close to edge on
GALAXY_RADIUS = 31.0
ARM_WINDING = 2.6            # How tightly the arms wrap around

RAMP = " .`':-~=+*#@"

CORE_COLOURS = (172, 178, 214, 220, 221, 222, 223, 228, 229, 230, 231)
BLUE_COLOURS = (24, 25, 26, 32, 33, 38, 39, 45, 75, 81, 117, 153)
PINK_COLOURS = (125, 162, 168, 204, 205, 211, 218)
DUST_COLOURS = (88, 94, 130, 131, 137)

BLUE_STARS = (33, 39, 75, 81, 117, 153)
RED_STARS = (124, 160, 167, 196, 203, 209)
WHITE_STARS = (246, 250, 252, 255, 231, 229)
STAR_CHANCE = 0.11
BRIGHT_STAR_CHANCE = 0.18

random.seed(20261107)
LATTICE = {}


def lattice(ix, iy):
    if (ix, iy) not in LATTICE:
        LATTICE[(ix, iy)] = random.random()
    return LATTICE[(ix, iy)]


def smooth_noise(x, y):
    """Blends random values on a grid, to give the arms a clumpy, irregular look."""
    ix = math.floor(x)
    iy = math.floor(y)
    fx = x - ix
    fy = y - iy
    fx = fx * fx * (3.0 - 2.0 * fx)
    fy = fy * fy * (3.0 - 2.0 * fy)
    top = lattice(ix, iy) + (lattice(ix + 1, iy) - lattice(ix, iy)) * fx
    bottom = lattice(ix, iy + 1) + (lattice(ix + 1, iy + 1) - lattice(ix, iy + 1)) * fx
    return top + (bottom - top) * fy


def galaxy(x, y):
    """Returns brightness, how much of it is the bulge, arm strength, and dust."""
    u = x * math.cos(ANGLE) + y * math.sin(ANGLE)
    v = (-x * math.sin(ANGLE) + y * math.cos(ANGLE)) / TILT_FLATTEN
    r = math.hypot(u, v) / GALAXY_RADIUS
    theta = math.atan2(v, u)

    bulge = 1.3 * math.exp(-((r / 0.17) ** 1.15))
    arm_phase = 2.0 * (theta - ARM_WINDING * math.log(max(r, 0.04)))
    arm = (0.5 + 0.5 * math.cos(arm_phase)) ** 3
    clumps = 0.55 + 0.9 * smooth_noise(u * 0.32 + 40.0, v * 0.32 + 40.0)
    edge = 1.0 - min(1.0, max(0.0, (r - 0.72) / 0.28))
    disk = math.exp(-r * 2.3) * (0.34 + 1.5 * arm * clumps) * edge * 1.15

    # Dust lanes lie just inside the arms, and show on the near side of the disk
    dust_arm = (0.5 + 0.5 * math.cos(arm_phase + 1.05)) ** 5
    dust = dust_arm * (0.6 if v > 0.0 else 0.3) * min(1.0, r / 0.12) * edge
    light = (bulge + disk) * (1.0 - 0.72 * dust)
    return light, bulge / (bulge + disk + 1e-6), arm * clumps, dust


def cell(col, row):
    light = bulge = arm = dust = 0.0
    for i in range(SAMPLES):
        for j in range(SAMPLES):
            x = col + (i + 0.5) / SAMPLES - CENTER_X
            y = (row + (j + 0.5) / SAMPLES - CENTER_Y) * CELL_ASPECT
            result = galaxy(x, y)
            light += result[0]
            bulge += result[1]
            arm += result[2]
            dust += result[3]
    count = SAMPLES * SAMPLES
    light /= count
    bulge /= count
    arm /= count
    dust /= count

    # Compress the range, as a long-exposure photograph does
    level = min(0.999, light ** 0.58)
    index = int(level * len(RAMP))
    if index == 0:
        return None
    char = RAMP[index]

    def pick(colours, position):
        return colours[min(len(colours) - 1, int(max(position, 0.0) * len(colours)))]

    # Old yellow stars at the centre, whitest in the very middle
    if bulge > 0.3:
        return char, pick(CORE_COLOURS, (bulge - 0.3) / 0.7)
    if dust > 0.2 and level < 0.8:
        return char, pick(DUST_COLOURS, level * 1.6)
    # Hot young stars glow pink where they form, scattered along the arms
    if arm > 0.45 and bulge < 0.1 and random.random() < 0.16:
        return random.choice("+*"), random.choice(PINK_COLOURS)
    # Where the centre's yellow light gives way to the blue disk
    if bulge > 0.12:
        return char, random.choice((180, 186, 187, 223, 224))
    return char, pick(BLUE_COLOURS, level * 1.75)


def companion(col, row):
    """Andromeda's two small companion galaxies, in about their real positions."""
    x = col + 0.5 - CENTER_X
    y = (row + 0.5 - CENTER_Y) * CELL_ASPECT
    # M32 is small and tight, just off the disk. M110 is larger, on the other side.
    if math.hypot(x - 9.0, y - 7.5) < 1.2:
        return "#", 223
    distance = math.hypot((x + 13.0) / 2.6, y + 11.0)
    if distance < 1.0:
        return ":", 180
    if distance < 1.6:
        return ".", 137
    return None


def star():
    colours = random.choice((BLUE_STARS, RED_STARS, WHITE_STARS))
    # Most are faint, with a few much brighter
    if random.random() < BRIGHT_STAR_CHANCE:
        return random.choice("*+"), colours[-1]
    return random.choice(".,'`."), random.choice(colours[:4])


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    lines = ["\033[?25h"]  # The kernel command line hides the cursor for the app
    for row in range(ROWS):
        text = "    "
        current = None
        for col in range(COLS):
            drawn = companion(col, row) or cell(col, row)
            if drawn is None and random.random() < STAR_CHANCE:
                drawn = star()
            if drawn is None:
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
