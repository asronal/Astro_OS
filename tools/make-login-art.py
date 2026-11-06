#!/usr/bin/env python3
"""Draw the login screen's Andromeda Galaxy, in plain ASCII with colour, into board/astro/issue.in.

Usage: make-login-art.py HYG.csv OUTPUT

HYG.csv is hygdata_v41.csv from https://github.com/astronexus/HYG-Database

What's real: the galaxy's size, shape and angle (OpenNGC), the positions of its
companions M32 and M110, its bright ring of star formation about 10 kpc out with a
fainter one at 15 kpc, dust on its near (north-west) side, and the position and
colour of every bright star (HYG). The view is turned to suit a wide screen, keeping
the sky unmirrored.

What's illustrative: the clumps along the rings, and the faintest background dots,
which stand in for the many stars too faint for the catalog.

Keep backslashes and percent signs out of the output, as getty treats both as escape
codes in /etc/issue.
"""

import csv
import math
import random
import sys

COLS = 72
ROWS = 17
CENTER_X = COLS / 2.0
CENTER_Y = ROWS / 2.0
CELL_ASPECT = 2.05  # A character cell is about twice as tall as it is wide
SAMPLES = 3
COLS_PER_DEGREE = 19.0
FIELD_TURN = math.radians(-69.0)

M31_RA = 10.6847
M31_DEC = 41.2691
M31_RADIUS_DEG = 177.83 / 120.0
M31_ANGLE_DEG = 35.0
M31_FLATTEN = 0.37  # Measured 0.39, which includes the rounder bulge
M32 = (10.6743, 40.8652)
M110 = (10.0920, 41.6853)

RAMP = " .`':-~=+*#@"
CORE_COLOURS = (172, 178, 214, 220, 221, 222, 223, 229, 230, 231)
INNER_COLOURS = (130, 136, 172, 173, 179, 180, 216)
BLUE_COLOURS = (25, 26, 32, 33, 39, 45, 75, 81, 117, 123, 153)
PINK_COLOURS = (168, 204, 205, 211, 212, 218)
DUST_COLOURS = (52, 88, 94, 130, 131)

random.seed(20261107)
LATTICE = {}


def lattice(ix, iy):
    if (ix, iy) not in LATTICE:
        LATTICE[(ix, iy)] = random.random()
    return LATTICE[(ix, iy)]


def smooth_noise(x, y):
    ix = math.floor(x)
    iy = math.floor(y)
    fx = x - ix
    fy = y - iy
    fx = fx * fx * (3.0 - 2.0 * fx)
    fy = fy * fy * (3.0 - 2.0 * fy)
    top = lattice(ix, iy) + (lattice(ix + 1, iy) - lattice(ix, iy)) * fx
    bottom = lattice(ix, iy + 1) + (lattice(ix + 1, iy + 1) - lattice(ix, iy + 1)) * fx
    return top + (bottom - top) * fy


def sky_to_screen(ra, dec):
    """A flat projection around M31, in columns and rows from the top left."""
    east = (ra - M31_RA) * math.cos(math.radians(M31_DEC))
    north = dec - M31_DEC
    # East is to the left and north is up, as on the real sky, then turned as a whole
    x = -east * math.cos(FIELD_TURN) + north * math.sin(FIELD_TURN)
    y = -east * math.sin(FIELD_TURN) - north * math.cos(FIELD_TURN)
    return CENTER_X + x * COLS_PER_DEGREE, CENTER_Y + y * COLS_PER_DEGREE / CELL_ASPECT


# The direction of the long axis on the screen, from the same projection
AXIS_X, AXIS_Y = sky_to_screen(M31_RA + math.sin(math.radians(M31_ANGLE_DEG)) / math.cos(math.radians(M31_DEC)),
                               M31_DEC + math.cos(math.radians(M31_ANGLE_DEG)))
AXIS_ANGLE = math.atan2((AXIS_Y - CENTER_Y) * CELL_ASPECT, AXIS_X - CENTER_X)


def galaxy(x, y):
    u = x * math.cos(AXIS_ANGLE) + y * math.sin(AXIS_ANGLE)
    v = -x * math.sin(AXIS_ANGLE) + y * math.cos(AXIS_ANGLE)
    # Keep "up the screen" as the near side, however the axis happens to point
    if math.cos(AXIS_ANGLE) < 0.0:
        u, v = -u, -v
    disk_v = v / M31_FLATTEN
    r = math.hypot(u, disk_v) / (M31_RADIUS_DEG * COLS_PER_DEGREE)

    bulge = 1.45 * math.exp(-((math.hypot(u, v * 1.45) / (M31_RADIUS_DEG * COLS_PER_DEGREE) / 0.13) ** 1.1))

    # The rings are real, and slightly off-centre and clumpy
    ring_r = math.hypot(u + 1.2, disk_v) / (M31_RADIUS_DEG * COLS_PER_DEGREE)
    clumps = 0.5 + 1.0 * smooth_noise(u * 0.42 + 40.0, disk_v * 0.13 + 40.0)
    ring = (math.exp(-(((ring_r - 0.52) / 0.075) ** 2)) + 0.62 * math.exp(-(((ring_r - 0.78) / 0.065) ** 2))) * clumps
    edge = 1.0 - min(1.0, max(0.0, (r - 0.8) / 0.34))
    disk = (0.52 * math.exp(-r * 2.6) + 0.34 * ring) * edge

    # Dust shows against the bright disk on the near side, within and beside the rings
    near = 1.0 if v < 0.0 else 0.28
    dust = near * (math.exp(-(((ring_r - 0.40) / 0.05) ** 2)) + 0.8 * math.exp(-(((ring_r - 0.65) / 0.05) ** 2))) * (0.6 + 0.6 * smooth_noise(u * 0.3 + 9.0, disk_v * 0.2 + 9.0))
    light = (bulge + disk) * (1.0 - 0.68 * min(dust, 1.0))
    return light, bulge / (bulge + disk + 1e-6), ring, dust, r


def cell(col, row):
    totals = [0.0] * 5
    for i in range(SAMPLES):
        for j in range(SAMPLES):
            x = col + (i + 0.5) / SAMPLES - CENTER_X
            y = (row + (j + 0.5) / SAMPLES - CENTER_Y) * CELL_ASPECT
            for k, value in enumerate(galaxy(x, y)):
                totals[k] += value
    light, bulge, ring, dust, r = [t / (SAMPLES * SAMPLES) for t in totals]

    # Compress the range, as a long-exposure photograph does
    level = min(0.999, light ** 0.55)
    index = int(level * len(RAMP))
    if index == 0:
        return None
    char = RAMP[index]

    def pick(colours, position):
        return colours[min(len(colours) - 1, int(max(position, 0.0) * len(colours)))]

    if bulge > 0.3:
        return char, pick(CORE_COLOURS, (bulge - 0.3) / 0.7)
    if dust > 0.34 and bulge < 0.25:
        return char, pick(DUST_COLOURS, level * 1.7)
    # Hot young stars light up pink clouds of hydrogen along the rings
    if ring > 0.5 and random.random() < 0.3:
        return random.choice("+*"), random.choice(PINK_COLOURS)
    if ring > 0.34:
        return char, pick(BLUE_COLOURS, 0.42 + level * 1.0)
    if r < 0.42:
        return char, pick(INNER_COLOURS, level * 1.35)
    return char, pick(BLUE_COLOURS, level * 2.0)


def star_colour(colour_index):
    if colour_index < 0.0:
        return random.choice((39, 75))
    if colour_index < 0.3:
        return random.choice((117, 153))
    if colour_index < 0.6:
        return random.choice((231, 255))
    if colour_index < 1.0:
        return random.choice((229, 223))
    if colour_index < 1.4:
        return random.choice((215, 209))
    return random.choice((203, 196))


def real_stars(path):
    stars = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            ra = float(row["ra"]) * 15.0
            dec = float(row["dec"])
            if abs(dec - M31_DEC) > 6.0 or abs(ra - M31_RA) > 9.0:
                continue
            column, line = sky_to_screen(ra, dec)
            key = (int(column), int(line))
            if not (0 <= key[0] < COLS and 0 <= key[1] < ROWS):
                continue
            mag = float(row["mag"])
            if key in stars and stars[key][0] <= mag:
                continue
            ci = float(row["ci"]) if row["ci"] else 0.5
            char = "*" if mag < 7.0 else "+" if mag < 8.5 else "'" if mag < 9.5 else "."
            stars[key] = (mag, char, star_colour(ci))
    print(f"{len(stars)} real stars in view")
    return stars


def companions(col, row):
    x, y = sky_to_screen(*M32)
    if int(x) == col and int(y) == row:
        return "#", 230
    if (int(x) - 1 == col or int(x) + 1 == col) and int(y) == row:
        return ":", 222
    x, y = sky_to_screen(*M110)
    # M110 is about 22 by 11 arcminutes, and nearly upright on the sky
    distance = math.hypot((col + 0.5 - x) / 3.4, (row + 0.5 - y) * CELL_ASPECT / 2.2)
    if distance < 0.45:
        return "=", 223
    if distance < 0.85:
        return ":", 180
    if distance < 1.25:
        return ".", 137
    return None


def faint_star():
    colours = random.choice(((33, 39, 75, 111), (160, 167, 203, 124), (246, 250, 252, 248)))
    return random.choice(".,`.."), random.choice(colours)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)

    stars = real_stars(sys.argv[1])
    lines = ["\033[?25h"]  # The kernel command line hides the cursor for the app
    for row in range(ROWS):
        text = "    "
        current = None
        for col in range(COLS):
            drawn = companions(col, row)
            real = stars.get((col, row))
            galaxy_cell = cell(col, row)
            if not drawn and real and (real[0] < 9.5 or galaxy_cell is None):
                drawn = real[1], real[2]
            if not drawn:
                drawn = galaxy_cell
            if not drawn and random.random() < 0.085:
                drawn = faint_star()
            if not drawn:
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

    with open(sys.argv[2], "w") as output:
        output.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
