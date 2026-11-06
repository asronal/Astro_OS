#!/usr/bin/env python3
"""Build the sky chart's data files from their open sources.

Usage: make-sky-data.py HYG.csv D3_CELESTIAL_DATA_DIR OPENNGC_DIR OUTPUT_DIR

HYG.csv is hygdata_v41.csv from https://github.com/astronexus/HYG-Database
D3_CELESTIAL_DATA_DIR holds constellations.json and constellations.lines.json
from https://github.com/ofrohn/d3-celestial/tree/master/data
OPENNGC_DIR holds NGC.csv and addendum.csv from https://github.com/mattiaverga/OpenNGC

Writes to OUTPUT_DIR:
  stars.csv             Adds each star's colour index to the existing file
  constellations.csv    One line segment per row, in J2000 degrees
  constellation-names.csv
  deepsky.csv           Adds each object's real size and angle to the existing file
  stars-faint.bin       Stars too faint for stars.csv, shown when zoomed in
  milkyway.png          Glow map of the sky, in J2000 degrees: x is RA, y is Dec

The Milky Way map is our own: it is the density of the faint HYG stars that are
too dim to draw one by one, smoothed so that they merge into a glow.
"""

import csv
import json
import math
import os
import struct
import sys
import zlib

MAP_W = 360
MAP_H = 180
FAINT_MAX_MAG = 9.0
GLOW_MIN_MAG = 6.5
GLOW_MAX_MAG = 11.0
SMOOTH_DEG = 2.2


def add_object_sizes(ngc_dir, path):
    known = {}
    for name in ("NGC.csv", "addendum.csv"):
        with open(os.path.join(ngc_dir, name), newline="") as f:
            for row in csv.DictReader(f, delimiter=";"):
                known[row["Name"]] = row

    def key(identifier):
        # deepsky.csv says "NGC 224", where OpenNGC says "NGC0224"
        parts = identifier.split()
        if len(parts) == 2 and parts[0] in ("NGC", "IC") and parts[1].isdigit():
            return f"{parts[0]}{int(parts[1]):04d}"
        return identifier.replace(" ", "")

    with open(path, newline="") as f:
        rows = list(csv.reader(f))
    out = [rows[0][:7] + ["maj_arcmin", "min_arcmin", "pa_deg"]]
    sized = 0
    for row in rows[1:]:
        entry = known.get(key(row[6]), {})
        major = entry.get("MajAx", "")
        minor = entry.get("MinAx", "") or major
        sized += major != ""
        out.append(row[:7] + [major, minor, entry.get("PosAng", "")])
    with open(path, "w", newline="") as f:
        csv.writer(f, lineterminator="\n").writerows(out)
    print(f"deepsky.csv: {len(out) - 1} objects, {sized} with a size")


def write_faint_stars(hyg_stars, path):
    # Six bytes each, as there are so many: RA, Dec, magnitude and colour index
    count = 0
    with open(path, "wb") as f:
        for s in sorted(hyg_stars, key=lambda star: star["mag"]):
            if GLOW_MIN_MAG < s["mag"] <= FAINT_MAX_MAG:
                ci = float(s["ci"]) if s["ci"] else 0.5
                f.write(struct.pack("<HhBb", int(s["ra"] % 360.0 / 360.0 * 65535.0 + 0.5),
                                    int(s["dec"] / 90.0 * 32767.0), int(s["mag"] * 20.0),
                                    int(max(-2.5, min(2.5, ci)) * 50.0)))
                count += 1
    print(f"stars-faint.bin: {count} stars")


def add_star_colours(hyg_stars, path):
    colour = {(round(s["ra"], 4), round(s["dec"], 4)): s["ci"] for s in hyg_stars}
    with open(path, newline="") as f:
        rows = list(csv.reader(f))
    # Keeping only the first four columns allows re-running on the output
    header = rows[0][:4] + ["ci"]
    missing = 0
    out = [header]
    for row in rows[1:]:
        ci = colour.get((round(float(row[0]), 4), round(float(row[1]), 4)), "")
        if ci == "":
            missing += 1
        out.append(row[:4] + [ci])
    with open(path, "w", newline="") as f:
        csv.writer(f, lineterminator="\n").writerows(out)
    print(f"stars.csv: {len(out) - 1} stars, {missing} without a colour")
    return [(float(r[0]), float(r[1])) for r in out[1:]]


def write_constellations(data_dir, out_dir, stars):
    with open(os.path.join(data_dir, "constellations.lines.json")) as f:
        lines = json.load(f)
    with open(os.path.join(data_dir, "constellations.json")) as f:
        names = json.load(f)

    segments = []
    for feature in lines["features"]:
        # Serpens appears twice in some data sets, which is harmless here
        for chain in feature["geometry"]["coordinates"]:
            for a, b in zip(chain, chain[1:]):
                segments.append([feature["id"], a[0] % 360.0, a[1], b[0] % 360.0, b[1]])

    with open(os.path.join(out_dir, "constellations.csv"), "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["con", "ra1_deg", "dec1_deg", "ra2_deg", "dec2_deg"])
        for s in segments:
            w.writerow([s[0]] + [f"{v:.4f}" for v in s[1:]])

    with open(os.path.join(out_dir, "constellation-names.csv"), "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["con", "name", "ra_deg", "dec_deg"])
        for feature in names["features"]:
            lon, lat = feature["geometry"]["coordinates"]
            w.writerow([feature["id"], feature["properties"]["name"], f"{lon % 360.0:.3f}", f"{lat:.3f}"])

    # Each line should end on a real star, so this catches any misread coordinates
    near = {}
    for ra, dec in stars:
        near.setdefault((int(ra), int(dec + 90)), []).append((ra, dec))
    ends = [(s[1], s[2]) for s in segments] + [(s[3], s[4]) for s in segments]
    matched = 0
    for ra, dec in ends:
        found = False
        for i in (-1, 0, 1):
            for j in (-1, 0, 1):
                for sra, sdec in near.get(((int(ra) + i) % 360, int(dec + 90) + j), ()):
                    dra = (sra - ra + 180.0) % 360.0 - 180.0
                    if math.hypot(dra * math.cos(math.radians(dec)), sdec - dec) < 0.3:
                        found = True
        matched += found
    print(f"constellations: {len(segments)} segments, {len(names['features'])} names, "
          f"{100.0 * matched / len(ends):.1f}% of line ends on a catalog star")


def box_blur_row(row, radius):
    n = len(row)
    radius = min(radius, (n - 1) // 2)
    width = 2 * radius + 1
    total = sum(row[i % n] for i in range(-radius, radius + 1))
    out = [0.0] * n
    for i in range(n):
        out[i] = total / width
        total += row[(i + radius + 1) % n] - row[(i - radius) % n]
    return out


def smooth(grid):
    # Three box blurs approximate a Gaussian. East-west distances shrink towards
    # the poles, so the blur gets wider in map pixels there.
    for _ in range(3):
        for y in range(MAP_H):
            dec = (y + 0.5) - 90.0
            radius = int(round(SMOOTH_DEG / max(math.cos(math.radians(dec)), 0.02)))
            grid[y] = box_blur_row(grid[y], radius)
        radius = int(round(SMOOTH_DEG))
        for x in range(MAP_W):
            column = [grid[y][x] for y in range(MAP_H)]
            # Reflect over the poles rather than wrapping from one to the other
            padded = column[radius - 1::-1] + column + column[:-radius - 1:-1]
            blurred = box_blur_row(padded, radius)[radius:-radius]
            for y in range(MAP_H):
                grid[y][x] = blurred[y]
    return grid


def write_grey_png(path, width, height, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    raw = b"".join(b"\0" + bytes(pixels[y]) for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def galactic_lat_lon(ra_deg, dec_deg):
    # J2000 north galactic pole and galactic centre
    ra = math.radians(ra_deg)
    dec = math.radians(dec_deg)
    ngp_ra = math.radians(192.85948)
    ngp_dec = math.radians(27.12825)
    sin_b = math.sin(dec) * math.sin(ngp_dec) + math.cos(dec) * math.cos(ngp_dec) * math.cos(ra - ngp_ra)
    y = math.cos(dec) * math.sin(ra - ngp_ra)
    x = math.sin(dec) * math.cos(ngp_dec) - math.cos(dec) * math.sin(ngp_dec) * math.cos(ra - ngp_ra)
    return math.degrees(math.asin(sin_b)), (122.93192 - math.degrees(math.atan2(y, x))) % 360.0


def write_milky_way(hyg_stars, out_dir):
    grid = [[0.0] * MAP_W for _ in range(MAP_H)]
    used = 0
    for s in hyg_stars:
        if GLOW_MIN_MAG < s["mag"] <= GLOW_MAX_MAG:
            x = int(s["ra"]) % MAP_W
            y = min(MAP_H - 1, int(s["dec"] + 90.0))
            # Each map pixel covers less sky towards the poles
            grid[y][x] += 1.0 / max(math.cos(math.radians(s["dec"])), 0.05)
            used += 1
    grid = smooth(grid)

    ordered = sorted(v for row in grid for v in row)
    low = ordered[int(len(ordered) * 0.40)]
    high = ordered[int(len(ordered) * 0.995)]
    pixels = []
    for row in grid:
        pixels.append([int(255.0 * min(1.0, max(0.0, (v - low) / (high - low))) ** 1.25 + 0.5) for v in row])
    write_grey_png(os.path.join(out_dir, "milkyway.png"), MAP_W, MAP_H, pixels)

    # The map must be bright along the galactic plane and dark at its poles
    def average(test):
        total = count = 0
        for y in range(MAP_H):
            weight = math.cos(math.radians(y + 0.5 - 90.0))
            for x in range(MAP_W):
                if test(*galactic_lat_lon(x + 0.5, y + 0.5 - 90.0)):
                    total += pixels[y][x] * weight
                    count += weight
        return total / count

    plane = average(lambda b, l: abs(b) < 5.0)
    poles = average(lambda b, l: abs(b) > 60.0)
    print(f"milkyway.png: {used} stars, brightness {plane:.0f} on the galactic plane, {poles:.0f} at its poles")
    if plane < poles * 3.0:
        sys.exit("ERROR: the Milky Way map does not follow the galactic plane")


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    hyg_path, data_dir, ngc_dir, out_dir = sys.argv[1:]

    hyg_stars = []
    with open(hyg_path, newline="") as f:
        for row in csv.DictReader(f):
            if row["proper"] == "Sol":
                continue
            hyg_stars.append({"ra": float(row["ra"]) * 15.0, "dec": float(row["dec"]),
                              "mag": float(row["mag"]), "ci": row["ci"]})
    print(f"HYG: {len(hyg_stars)} stars")

    stars = add_star_colours(hyg_stars, os.path.join(out_dir, "stars.csv"))
    write_constellations(data_dir, out_dir, stars)
    add_object_sizes(ngc_dir, os.path.join(out_dir, "deepsky.csv"))
    write_faint_stars(hyg_stars, os.path.join(out_dir, "stars-faint.bin"))
    write_milky_way(hyg_stars, out_dir)


if __name__ == "__main__":
    main()
