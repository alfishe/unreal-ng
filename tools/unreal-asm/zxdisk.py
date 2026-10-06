#!/usr/bin/env python3
"""TR-DOS disk images for the unreal-asm checks: read TRD / SCL catalogs, write hobeta files, add files to a TRD.

    zxdisk.py list    <image.trd|.scl>
    zxdisk.py extract <image> <NAME.T> <out.$T>       a file as hobeta (the last one of that name, as TASM / ALASM read)
    zxdisk.py add     <image.trd> <out.trd> <file.$T>...   the hobeta files appended after the last used sector
    zxdisk.py scl2trd <image.scl> <out.trd>

Used as a module by the other tools: files(image_bytes) yields (name, type, start, length, sector_bytes).
"""
import sys


def files(img):
    """(name, type, start, length, data) for every file; data is whole sectors (the bytes after `length` included)"""
    if img[:8] == b'SINCLAIR':
        n = img[8]
        p = 9 + n * 14
        for i in range(n):
            e = img[9 + i * 14:9 + i * 14 + 14]
            size = e[13] * 256
            yield e[:8].decode('latin1').rstrip(), chr(e[8]), e[9] | e[10] << 8, e[11] | e[12] << 8, img[p:p + size]
            p += size
        return
    for i in range(128):
        e = img[i * 16:i * 16 + 16]
        if e[0] == 0:
            break
        if e[0] == 1:   # deleted
            continue
        off = (e[15] * 16 + e[14]) * 256
        yield e[:8].decode('latin1').rstrip(), chr(e[8]), e[9] | e[10] << 8, e[11] | e[12] << 8, img[off:off + e[13] * 256]


def hobeta(name, type_, start, length, data):
    """A hobeta file: name(8) type(1) start(2) length(2) 0 sectors(1) checksum(2), then whole sectors"""
    sectors = (len(data) + 255) // 256
    header = bytearray(name.ljust(8).encode('latin1')[:8] + type_.encode('latin1') +
                       bytes([start & 255, start >> 8, length & 255, length >> 8, 0, sectors]))
    checksum = sum(b * 257 + i for i, b in enumerate(header[:15])) & 0xFFFF
    return bytes(header) + bytes([checksum & 255, checksum >> 8]) + data.ljust(sectors * 256, b'\0')


def add(img, hobeta_files):
    """The image with each hobeta file appended after the last used sector"""
    img = bytearray(img)
    info = 8 * 256
    for data in hobeta_files:
        header, body = data[:17], data[17:]
        sectors = header[14]
        sec, trk, count = img[info + 0xE1], img[info + 0xE2], img[info + 0xE4]
        img[count * 16:count * 16 + 16] = bytes(header[:13]) + bytes([sectors, sec, trk])
        off = (trk * 16 + sec) * 256
        img[off:off + sectors * 256] = body[:sectors * 256].ljust(sectors * 256, b'\0')
        pos = trk * 16 + sec + sectors
        img[info + 0xE1], img[info + 0xE2], img[info + 0xE4] = pos % 16, pos // 16, count + 1
        free = (img[info + 0xE5] | img[info + 0xE6] << 8) - sectors
        img[info + 0xE5], img[info + 0xE6] = free & 255, free >> 8
    return bytes(img)


def scl2trd(scl):
    """A TRD image (80 tracks, 2 sides) holding the files of an SCL image"""
    assert scl[:8] == b'SINCLAIR'
    n = scl[8]
    img = bytearray(655360)
    p, pos = 9 + n * 14, 16   # data from track 1
    for i in range(n):
        e = scl[9 + i * 14:9 + i * 14 + 14]
        secs = e[13]
        img[i * 16:i * 16 + 14] = e
        img[i * 16 + 14], img[i * 16 + 15] = pos % 16, pos // 16
        img[pos * 256:pos * 256 + secs * 256] = scl[p:p + secs * 256]
        p += secs * 256
        pos += secs
    info = 8 * 256
    img[info + 0xE1], img[info + 0xE2], img[info + 0xE3], img[info + 0xE4] = pos % 16, pos // 16, 0x16, n
    free = 2560 - pos
    img[info + 0xE5], img[info + 0xE6], img[info + 0xE7] = free & 255, free >> 8, 0x10
    img[info + 0xF5:info + 0xFD] = b'        '
    return bytes(img)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    command, image = argv[1], open(argv[2], 'rb').read()
    if command == 'list':
        for name, type_, start, length, data in files(image):
            print(f'{name}.{type_}\t{start}\t{length}\t{len(data) // 256}')
    elif command == 'extract' and len(argv) == 5:
        name, _, type_ = argv[3].partition('.')
        found = [f for f in files(image) if f[0] == name and (not type_ or f[1] == type_)]
        if not found:
            print(f'{argv[3]}: not on the image')
            return 1
        open(argv[4], 'wb').write(hobeta(*found[-1]))
    elif command == 'add' and len(argv) >= 5:
        open(argv[3], 'wb').write(add(image, [open(f, 'rb').read() for f in argv[4:]]))
    elif command == 'scl2trd' and len(argv) == 4:
        open(argv[3], 'wb').write(scl2trd(image))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
