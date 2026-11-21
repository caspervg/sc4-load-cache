"""Runs the raw ExemplarType reader over every exemplar of real plugin folders.

    python tests/check_exemplar_types.py <exemplar_type_test.exe> <folder> [<folder> ...]

Each exemplar record (decompressed when the file's directory record lists it, as the game does)
is given to exemplar_type_test.exe and to a second reader written here from the notes in
docs/re-notes.md. Both must agree on every record. Where they give a type, a third reader that
only knows the usual description of the exemplar format (id, value type, key type, rep count)
must find the same value. Prints how many records the raw reader answers and why not the rest.
"""
import collections
import os
import re
import struct
import subprocess
import sys
import tempfile

EXEMPLAR = 0x6534284A
DIRECTORY = (0xE86B1EEF, 0xE86B1EEF, 0x286B1F03)
SINGLE = {0: 0, 1: 1, 2: 2, 3: 4, 4: 8, 5: 1, 6: 2, 7: 4, 8: 8, 9: 4, 10: 8, 11: 1, 12: 1}
ARRAY = {0x8000: 1, 0x8001: 1, 0x8002: 2, 0x8003: 4, 0x8004: 8, 0x8005: 1, 0x8006: 2, 0x8007: 4,
         0x8008: 8, 0x8009: 4, 0x800A: 8, 0x800B: 1, 0x8010: 2, 0x8011: 1}


def qfs(d):
    """A record as stored: compressed size, 0x10FB, 3 bytes of length, then the packed data."""
    out = bytearray()
    i = 9
    while i < len(d):
        c = d[i]
        if c < 0x80:
            plain, count, offset = c & 3, ((c & 0x1C) >> 2) + 3, ((c & 0x60) << 3) + d[i + 1] + 1
            i += 2
        elif c < 0xC0:
            plain, count, offset = d[i + 1] >> 6, (c & 0x3F) + 4, ((d[i + 1] & 0x3F) << 8) + d[i + 2] + 1
            i += 3
        elif c < 0xE0:
            plain, count = c & 3, ((c & 0x0C) << 6) + d[i + 3] + 5
            offset = ((c & 0x10) << 12) + (d[i + 1] << 8) + d[i + 2] + 1
            i += 4
        elif c < 0xFC:
            plain = ((c & 0x1F) << 2) + 4
            out += d[i + 1:i + 1 + plain]
            i += 1 + plain
            continue
        else:
            out += d[i + 1:i + 1 + (c & 3)]
            break
        out += d[i:i + plain]
        i += plain
        start = len(out) - offset
        if offset >= count:
            out += out[start:start + count]
        else:
            for k in range(count):
                out.append(out[start + k])
    return bytes(out)


def exemplars(path):
    """(key, record bytes as the game's reader gets them) for every exemplar of a DBPF file."""
    with open(path, "rb") as f:
        head = f.read(96)
        if len(head) < 96 or head[:4] != b"DBPF":
            return
        count, offset = struct.unpack_from("<II", head, 0x24)
        f.seek(offset)
        index = list(struct.iter_unpack("<IIIII", f.read(count * 20)))
        packed = set()
        for t, g, i, at, size in index:
            if (t, g, i) == DIRECTORY:
                f.seek(at)
                data = f.read(size)
                packed = {struct.unpack_from("<III", data, k * 16) for k in range(len(data) // 16)}
        for t, g, i, at, size in index:
            if t == EXEMPLAR:
                f.seek(at)
                data = f.read(size)
                yield (t, g, i), qfs(data) if (t, g, i) in packed else data


def to_uint(s):
    """The game's number reader (0x0090fe93), None where the raw reader gives up."""
    if not s:
        return 0
    if s[0] <= 0x20 or s[0] >= 0x80 or s[0] in b"+-":
        return None
    if s[:2] in (b"0x", b"0X"):
        radix, s = 16, s[2:]
    else:
        radix = 10
        for c in s:
            if c in b"abcdefABCDEF":
                radix = 16
                break
            if c not in b"0123456789":
                break
    digits = b"0123456789" if radix == 10 else b"0123456789abcdefABCDEF"
    n = 0
    while n < len(s) and s[n] in digits:
        n += 1
    return min(int(s[:n], radix), 0xFFFFFFFF) if n else 0


def second_reader(d):
    """(type or None, why not): the walk of docs/re-notes.md, written again."""
    if len(d) >= 8 and d[:4] == b"EQZB":
        if len(d) < 24:
            return None, "cut off"
        count, = struct.unpack_from("<I", d, 20)
        at, found = 24, None
        for _ in range(count):
            if at + 5 > len(d):
                return None, "cut off"
            prop, kind = struct.unpack_from("<IB", d, at)
            at += 5
            if kind == 1 and prop != 0x10:
                at += 12
                if at > len(d):
                    return None, "cut off"
                continue
            if kind != 0:
                return None, "type is a resource key" if kind == 1 else "unknown property kind"
            if at + 4 > len(d):
                return None, "cut off"
            code, = struct.unpack_from("<I", d, at)
            at += 4
            if prop == 0x10 and code != 3:
                return None, "type is not one Uint32 (variant code 0x%x)" % code
            if code in SINGLE:
                size = SINGLE[code]
            elif code == 0x800C or code in ARRAY:
                if at + 4 > len(d):
                    return None, "cut off"
                size, = struct.unpack_from("<I", d, at)
                at += 4
                if code == 0x800C and size > 200000:
                    return None, "string length"
                size *= ARRAY.get(code, 1)
            else:
                return None, "unknown variant code"
            if at + size > len(d):
                return None, "cut off"
            if prop == 0x10:
                found, = struct.unpack_from("<I", d, at)
            at += size
        return (found, "") if found is not None else (None, "no type of its own (parent cohort)")
    if len(d) >= 10 and d[:4] == b"EQZT":
        if b"\0" in d[10:]:
            return None, "text with a zero byte"
        entries = []
        for line in d[10:].split(b"\n"):
            line = line[:-1] if line.endswith(b"\r") else line
            if line:
                name, equals, value = line.partition(b"=")
                entries.append((name.strip(b" \t"), value.strip(b" \t")) if equals else None)
        if len(entries) < 2 or None in entries[:2] or entries[0][0].lower() != b"parentcohort" or \
                entries[1][0].lower() != b"propcount":
            return None, "text header"
        count = to_uint(entries[1][1])
        if count is None:
            return None, "text number"
        found = None
        for i in range(count):
            if i + 2 >= len(entries) or entries[i + 2] is None:
                return None, "text cut off"
            name, value = entries[i + 2]
            prop = to_uint(name)
            if prop is None:
                return None, "text number"
            if prop == 0x10:
                kind, _, rest = value.partition(b":")
                reps, colon, values = rest.partition(b":")
                if kind != b"Uint32" or not colon or to_uint(rest) != 0 or values[:1] != b"{":
                    return None, "text type is not one Uint32"
                found = to_uint(values[1:])
                if found is None:
                    return None, "text number"
        return (found, "") if found is not None else (None, "no type of its own (parent cohort)")
    return None, "not an exemplar header"


SIZES = {0x100: 1, 0x200: 2, 0x300: 4, 0x700: 4, 0x800: 8, 0x900: 4, 0xB00: 1, 0xC00: 1}
TEXT_TYPE = re.compile(rb'^[ \t]*0x0*10:\{[^\r\n]*\}=Uint32:0:\{(0x[0-9a-fA-F]+)\}[ \t]*\r?$', re.M)


def usual_reader(d):
    """The type by the usual description of the format, or None when that does not fit."""
    if d[:4] == b"EQZT":
        found = TEXT_TYPE.findall(d)
        return int(found[-1], 16) if found else None
    try:
        count, = struct.unpack_from("<I", d, 20)
        at, found = 24, None
        for _ in range(count):
            prop, value_type, key_type = struct.unpack_from("<IHH", d, at)
            at += 9
            reps = 1
            if key_type == 0x80:
                reps, = struct.unpack_from("<I", d, at)
                at += 4
            if prop == 0x10:
                found = struct.unpack_from("<I", d, at)[0] if (value_type, key_type) == (0x300, 0) else None
            at += reps * SIZES[value_type]
        return found
    except (struct.error, KeyError):
        return None


def main():
    exe, folders = os.path.abspath(sys.argv[1]), sys.argv[2:]
    expected, why, files, wrong = [], collections.Counter(), 0, 0
    kinds = collections.Counter()
    with tempfile.TemporaryDirectory() as temp:
        dump_path = os.path.join(temp, "exemplars.bin")
        with open(dump_path, "wb") as dump:
            for folder in folders:
                for directory, _, names in os.walk(folder):
                    for name in sorted(names):
                        path = os.path.join(directory, name)
                        try:
                            records = list(exemplars(path))
                        except (OSError, struct.error, IndexError) as error:
                            print("skipped", path, error)
                            continue
                        files += bool(records)
                        for key, data in records:
                            dump.write(struct.pack("<IIII", *key, len(data)) + data)
                            found, reason = second_reader(data)
                            expected.append((key, found))
                            why[reason or "answered"] += 1
                            kinds[data[:4] if data[:4] in (b"EQZB", b"EQZT") else b"other"] += 1
                            if found is not None and usual_reader(data) != found:
                                wrong += 1
                                print("usual reader differs: %08X %08X %08X in %s" % (*key, path))
        lines = subprocess.run([exe, dump_path], capture_output=True, text=True, check=True).stdout.split("\n")
    for (key, found), line in zip(expected, lines):
        want = "%08X %08X %08X %s" % (*key, "-" if found is None else "%08X" % found)
        if line != want:
            wrong += 1
            print("differs: test program", line, "second reader", want)
    wrong += len(lines) - 1 != len(expected)
    total = len(expected)
    print("%d exemplars in %d files: %s" % (total, files, dict(kinds)))
    for reason, count in why.most_common():
        print("%8d %5.1f%%  %s" % (count, 100.0 * count / max(total, 1), reason))
    print("%d differences" % wrong)
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
