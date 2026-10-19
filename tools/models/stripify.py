"""Triangle strips for FSM4 meshes (docs/formats/fsm4.md, docs/decisions/0049).

The PSP GE transforms and lights every index it is sent - there is no
post-transform vertex cache (docs/psp/gotchas.md #31) - so an indexed
triangle list costs 3 vertices per triangle. Re-laid as strips, the same
triangles cost ~1.5-2. Each part becomes one strip (runs joined by repeated
indices; winding parity kept), flagged in its record (bit 0 of the reserved
word); gfx::Mesh draws flagged parts as GU_TRIANGLE_STRIP.

Usage: python3 tools/models/stripify.py assets/models/su35s.fsm [more.fsm ...]
Also called by fmodel.py on export. Idempotent: flagged parts are left alone.
"""

import struct
import sys

STRIP_FLAG = 1


def _canon(t):
    """A triangle with its winding, independent of the starting corner."""
    a, b, c = t
    m = min(t)
    if m == a:
        return (a, b, c)
    if m == b:
        return (b, c, a)
    return (c, a, b)


def stripify(tris):
    """tris: list of (a, b, c) counter-clockwise. Returns one strip (list of
    indices) drawing exactly these triangles with the same winding."""
    n = len(tris)
    if n == 0:
        return []
    used = [False] * n
    by_edge = {}
    for i, (a, b, c) in enumerate(tris):
        for e in ((a, b), (b, c), (c, a)):
            by_edge.setdefault(e, []).append(i)

    def neighbours(i):
        a, b, c = tris[i]
        return sum(len(by_edge.get((v, u), ())) for u, v in ((a, b), (b, c), (c, a)))

    def find(u, v, even, taken):
        """An unused triangle continuing the strip after (u, v): at an even
        position it is (u, v, w) CCW, at an odd one (v, u, w)."""
        edge = (u, v) if even else (v, u)
        for j in by_edge.get(edge, ()):
            if used[j] or j in taken:
                continue
            a, b, c = tris[j]
            for x, y, z in ((a, b, c), (b, c, a), (c, a, b)):
                if (x, y) == edge:
                    return j, z
        return None, None

    def grow(start, rot):
        a, b, c = tris[start]
        strip = list([(a, b, c), (b, c, a), (c, a, b)][rot])
        taken = {start}
        while True:
            j, w = find(strip[-2], strip[-1], (len(strip) - 2) % 2 == 0, taken)
            if j is None:
                return strip, taken
            taken.add(j)
            strip.append(w)

    # Start where the mesh is thinnest (fewest neighbours) - edges and seams
    # first, so strips don't strand single triangles in the middle.
    order = sorted(range(n), key=neighbours)
    out, cursor = [], 0
    while cursor < n:
        while cursor < n and used[order[cursor]]:
            cursor += 1
        if cursor >= n:
            break
        start = order[cursor]
        best_strip, best_taken = None, None
        for rot in range(3):
            strip, taken = grow(start, rot)
            if best_taken is None or len(taken) > len(best_taken):
                best_strip, best_taken = strip, taken
        for j in best_taken:
            used[j] = True
        if out:
            # Join: repeat the last index and the new strip's first; one more
            # repeat if the new strip would start on an odd position.
            out += [out[-1], best_strip[0]]
            if len(out) % 2 == 1:
                out.append(best_strip[0])
        out += best_strip
    return out


def strip_triangles(strip):
    """The triangles a strip draws (degenerate ones skipped), CCW."""
    tris = []
    for k in range(len(strip) - 2):
        a, b, c = strip[k], strip[k + 1], strip[k + 2]
        if a == b or b == c or a == c:
            continue
        tris.append((a, b, c) if k % 2 == 0 else (b, a, c))
    return tris


def process(path):
    data = bytearray(open(path, "rb").read())
    magic, nv, ni, radius, nparts = struct.unpack_from("<4sIIfI", data, 0)
    assert magic == b"FSM4", path
    parts = []
    for p in range(nparts):
        off = 32 + 48 * p
        first, count, channel, side, flags = struct.unpack_from("<IIHHI", data, off)
        parts.append([first, count, channel, side, flags, off])
    vbytes = 32 * nv
    ioff = 32 + 48 * nparts + vbytes
    index = list(struct.unpack_from(f"<{ni}H", data, ioff))
    new_index, before, after = [], 0, 0
    for part in parts:
        first, count, channel, side, flags, off = part
        seg = index[first:first + count]
        if flags & STRIP_FLAG:
            tris = strip_triangles(seg)
            if len(tris) * 3 >= len(seg):
                part[0] = len(new_index)
                new_index += seg
                continue
            # A strip that came out longer than its list: back to the list.
            flat = [i for t in tris for i in t]
            part[0], part[1], part[4] = len(new_index), len(flat), flags & ~STRIP_FLAG
            new_index += flat
            continue
        tris = [tuple(seg[k:k + 3]) for k in range(0, count, 3)]
        strip = stripify(tris)
        got = sorted(_canon(t) for t in strip_triangles(strip))
        want = sorted(_canon(t) for t in tris if len(set(t)) == 3)
        assert got == want, f"{path}: strip of part {channel}/{side} draws other triangles"
        before += count
        if len(strip) >= count:  # poorly connected (split normals, few shared edges): no gain
            after += count
            part[0] = len(new_index)
            new_index += seg
            continue
        after += len(strip)
        part[0], part[1], part[4] = len(new_index), len(strip), flags | STRIP_FLAG
        new_index += strip
    head = bytes(data[:32 + 48 * nparts + vbytes])
    head = bytearray(head)
    struct.pack_into("<I", head, 8, len(new_index))
    for first, count, channel, side, flags, off in parts:
        struct.pack_into("<IIHHI", head, off, first, count, channel, side, flags)
    body = bytes(head) + struct.pack(f"<{len(new_index)}H", *new_index)
    body += b"\0" * ((-len(body)) % 16)
    open(path, "wb").write(body)
    tris = before // 3
    if tris:
        print(f"[stripify] {path}: {tris} triangles, {before} -> {after} indices ({after / tris:.2f} per triangle)")
    return before, after


if __name__ == "__main__":
    for p in sys.argv[1:]:
        process(p)
