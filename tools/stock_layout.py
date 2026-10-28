"""Read-only validation of the supported TCL flash layout for first installation."""
import hashlib
import struct
import zlib

FLASH_SIZE = 0x200000
TABLE_OFFSET = 0x11000
HEADER_OFFSET = 0x17F00


def check(condition, message):
    if not condition:
        raise ValueError(message)


def parse_tcl_table(data):
    table = data[TABLE_OFFSET:TABLE_OFFSET + 0x1000]
    check(len(table) == 0x1000 and table[:4] == b"THOS", "Missing TCL partition table")
    version, count, length = struct.unpack_from("<HBI", table, 4)
    end = 11 + length
    check(version == 1 and 0 < count <= 16 and end + 4 <= len(table), "Invalid TCL table bounds")
    check(zlib.crc32(table[:end]) == struct.unpack_from("<I", table, end)[0], "TCL table CRC mismatch")
    result = {}
    pos = 11
    for _ in range(count):
        check(pos + 7 <= end, "Truncated TCL partition record")
        size, _ident, _flags, name_length = struct.unpack_from("<HHBH", table, pos)
        check(size == 15 + name_length and pos + size <= end, "Invalid TCL partition record length")
        name = table[pos + 7:pos + 7 + name_length].decode("ascii")
        start, length = struct.unpack_from("<II", table, pos + 7 + name_length)
        check(name not in result and length > 0 and start + length <= len(data), "Invalid TCL partition range")
        result[name] = {"start": start, "length": length}
        pos += size
    check(pos == end, "Unconsumed TCL partition records")
    ordered = sorted(result.values(), key=lambda region: region["start"])
    check(all(a["start"] + a["length"] <= b["start"] for a, b in zip(ordered, ordered[1:])),
          "Overlapping TCL partitions")
    return result


def parse_tcl_image(data, offset=HEADER_OFFSET):
    header = data[offset:offset + 0x100]
    check(len(header) == 0x100 and header[:4] == b"THOS", "Missing TCL application header")
    check(zlib.crc32(header[:252]) == struct.unpack_from("<I", header, 252)[0], "TCL header CRC mismatch")
    header_length = struct.unpack_from("<H", header, 4)[0]
    check(46 <= header_length <= 252, "Invalid TCL application header length")
    pos = 6
    names = []
    for _ in range(2):
        check(pos + 2 <= header_length, "Truncated TCL image name length")
        length = struct.unpack_from("<H", header, pos)[0]
        pos += 2
        check(0 < length <= 32 and pos + length <= header_length, "Invalid TCL image name length")
        name = header[pos:pos + length].decode("ascii")
        check("V8-R" in name, "Missing stock bootloader image-name marker")
        names.append(name)
        pos += length
    check(pos + 36 == header_length, "Unexpected TCL application header structure")
    length = struct.unpack_from("<I", header, pos)[0]
    start = offset + 0x100
    check(length > 0 and start + length <= len(data), "Invalid TCL payload bounds")
    expected_md5 = header[pos + 4:pos + 36].decode("ascii")
    check(hashlib.md5(data[start:start + length]).hexdigest() == expected_md5, "TCL payload MD5 mismatch")
    return {"names": names, "start": start, "length": length}
