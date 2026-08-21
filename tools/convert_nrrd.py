#!/usr/bin/env python3
"""Setup-time NRRD downsampler (SPEC S7).

Stdlib-only. Reads a NRRD file (text header + raw or gzip voxel block),
block-averages it so every axis is <= 128, and re-writes it as a raw NRRD.

Usage: python3 tools/convert_nrrd.py <input.nrrd> <output.nrrd>
"""

import gzip
import sys
from array import array

MAX_DIM = 128

TYPE_MAP = {
    "int8": "b",
    "uint8": "B",
    "int16": "h",
    "uint16": "H",
    "int32": "i",
    "int": "i",
    "uint32": "I",
    "int64": "q",
    "uint64": "Q",
    "float": "f",
    "double": "d",
}


def parse_nrrd(path):
    header = []
    with open(path, "rb") as fh:
        while True:
            line = fh.readline()
            if not line or line.strip() == b"":
                break
            header.append(line.rstrip(b"\n").decode("ascii"))
        raw = fh.read()
    fields = {}
    for line in header:
        if not line or line.startswith("#") or ":" not in line:
            continue
        key, _, value = line.partition(":")
        fields[key.strip()] = value.strip()
    return fields, raw


def parse_vec(text):
    text = text.replace("(", "").replace(")", "")
    return [float(x) for x in text.split(",")]


def main():
    if len(sys.argv) != 3:
        print("usage: convert_nrrd.py <input.nrrd> <output.nrrd>")
        return 2

    fields, raw = parse_nrrd(sys.argv[1])

    if fields["dimension"] != "3":
        print("error: only 3D NRRD supported")
        return 1

    sizes = [int(x) for x in fields["sizes"].split()]
    if len(sizes) != 3:
        print("error: expected 3 sizes")
        return 1

    typecode = TYPE_MAP.get(fields["type"])
    if typecode is None:
        print("error: unsupported type '%s'" % fields["type"])
        return 1

    encoding = fields.get("encoding", "raw")
    if encoding in ("gzip", "gz"):
        data = gzip.decompress(raw)
    elif encoding == "raw":
        data = raw
    else:
        print("error: unsupported encoding '%s'" % encoding)
        return 1

    itemsize = array(typecode).itemsize
    expected = sizes[0] * sizes[1] * sizes[2] * itemsize
    if len(data) < expected:
        print("error: voxel block too short (%d < %d bytes)" % (len(data), expected))
        return 1
    data = data[:expected]

    src = array(typecode)
    src.frombytes(data)

    factors = [max(1, (size + MAX_DIM - 1) // MAX_DIM) for size in sizes]
    out_sizes = [(size + factor - 1) // factor for size, factor in zip(sizes, factors)]
    if max(out_sizes) > MAX_DIM:
        print("error: output dims exceed %d" % MAX_DIM)
        return 1

    sx, sy, sz = sizes
    fx, fy, fz = factors
    ox, oy, oz = out_sizes
    is_float = typecode in ("f", "d")
    out = array(typecode)

    for zo in range(oz):
        z_lo, z_hi = zo * fz, min(sz, (zo + 1) * fz)
        for yo in range(oy):
            y_lo, y_hi = yo * fy, min(sy, (yo + 1) * fy)
            for xo in range(ox):
                x_lo, x_hi = xo * fx, min(sx, (xo + 1) * fx)
                total = 0
                for z in range(z_lo, z_hi):
                    row_base = (z * sy) * sx
                    for y in range(y_lo, y_hi):
                        total += sum(src[row_base + y * sx + x_lo: row_base + y * sx + x_hi])
                count = (z_hi - z_lo) * (y_hi - y_lo) * (x_hi - x_lo)
                if is_float:
                    out.append(total / count)
                else:
                    out.append(round(total / count))

    header = ["NRRD0004", "type: %s" % fields["type"], "dimension: 3"]
    if "space" in fields:
        header.append("space: %s" % fields["space"])
    header.append("sizes: %d %d %d" % (ox, oy, oz))
    if "space directions" in fields:
        vecs = [parse_vec(v) for v in fields["space directions"].split(")") if v.strip()]
        scaled = [
            "(%s,%s,%s)" % tuple("%.12g" % (c * factors[i]) for c in vecs[i])
            for i in range(3)
        ]
        header.append("space directions: " + " ".join(scaled))
    if "kinds" in fields:
        header.append("kinds: %s" % fields["kinds"])
    if "endian" in fields:
        header.append("endian: %s" % fields["endian"])
    header.append("encoding: raw")
    if "space origin" in fields:
        header.append("space origin: %s" % fields["space origin"])

    with open(sys.argv[2], "wb") as fh:
        fh.write(("\n".join(header) + "\n\n").encode("ascii"))
        fh.write(out.tobytes())

    print("wrote %s: %d x %d x %d (%s, %d bytes raw block)"
          % (sys.argv[2], ox, oy, oz, fields["type"], len(out.tobytes())))
    return 0


if __name__ == "__main__":
    sys.exit(main())