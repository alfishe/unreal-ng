"""Teledisk "advanced compression" (TD0 files with the lowercase "td" signature).

Everything after the 12-byte header is one LZHUF stream (Okumura / Yoshizaki LZSS + adaptive Huffman):
a 4 KB ring buffer pre-filled with spaces, matches of 3..60 bytes, 314 adaptively coded symbols
(256 literals + 58 match lengths) and match positions coded as a fixed 3..8-bit prefix for the upper 6 bits
plus 6 raw bits. Port of LzhufDecoder in core/src/3rdparty/unreal-disk/src/loaders/loader_td0.cpp; the
decoder behaves the same bit for bit:

- the stream has no length prefix: decoding runs while unread input bits remain;
- reads past the end yield zero bits (the original getc() -> 0), and a symbol started inside the last byte
  is finished with those zeros, so the output may carry a few junk bytes after the TD0 end marker (0xFF)
  exactly like the C++ loader's buffer (its body parser stops at the marker);
- output stops at max_out bytes (64 MB, LoaderTD0::MAX_DECOMPRESSED_SIZE).

The LZHUF stream itself carries no check data, so a corrupt stream decodes to garbage rather than failing.
decompress_checked() walks the decoded TD0 body the way LoaderTD0::parseBody does and raises ValueError
where the C++ loader rejects the image (truncated inside a track header, sector header or data block).

Only decoding exists: LoaderTD0 writes uncompressed "TD" images, so there is no compressor to mirror.
"""

from __future__ import annotations

__all__ = ["decompress", "decompress_checked", "body_length", "MAX_DECOMPRESSED_SIZE"]

MAX_DECOMPRESSED_SIZE = 64 * 1024 * 1024

_N = 4096  # ring buffer size
_F = 60  # longest match
_THRESHOLD = 2
_N_CHAR = 256 - _THRESHOLD + _F  # 314 symbols
_T = _N_CHAR * 2 - 1  # 627 tree nodes
_R = _T - 1  # root
_MAX_FREQ = 0x8000

# TD0 body layout (LoaderTD0 constants)
_COMMENT_HEADER_SIZE = 10
_TRACK_HEADER_SIZE = 4
_SECTOR_HEADER_SIZE = 6
_END_OF_IMAGE = 0xFF
_FLAGS_NO_DATA_BLOCK = 0x10 | 0x20


def _position_tables() -> tuple[bytes, bytes]:
    """Canonical prefix code with 1/3/8/12/24/16 codes of lengths 3..8 for the upper 6 bits of a position.
    Indexed by the next 8 input bits: (upper 6 bits value, prefix length)."""
    d_code = bytearray(256)
    d_len = bytearray(256)
    index = 0
    code = 0
    prev_len = 3
    for li, count in enumerate((1, 3, 8, 12, 24, 16)):
        length = 3 + li
        for _ in range(count):
            if index > 0:
                code = (code + 1) << (length - prev_len)
            prev_len = length
            first = code << (8 - length)
            for b in range(first, first + (1 << (8 - length))):
                d_code[b] = index
                d_len[b] = length
            index += 1
    return bytes(d_code), bytes(d_len)


_D_CODE, _D_LEN = _position_tables()
# One byte per input bit (MSB first), built with a lookup per input byte
_BIT_TABLE = [bytes((b >> (7 - k)) & 1 for k in range(8)) for b in range(256)]
_PAD = 64  # zero bytes appended so a symbol finished past the end reads zeros without bounds checks


def _reconstruct(freq: list[int], son: list[int], prnt: list[int]) -> None:
    """Halve all leaf frequencies and rebuild the tree (keeps the sibling property)"""
    j = 0
    for i in range(_T):
        if son[i] >= _T:
            freq[j] = (freq[i] + 1) // 2
            son[j] = son[i]
            j += 1

    i = 0
    j = _N_CHAR
    while j < _T:
        f = (freq[i] + freq[i + 1]) & 0xFFFF
        freq[j] = f
        k = j - 1
        while f < freq[k]:
            k -= 1
        k += 1
        # memmove(&freq[k + 1], &freq[k], j - k); freq[k] = f (and the same for son)
        freq[k + 1 : j + 1] = freq[k:j]
        freq[k] = f
        son[k + 1 : j + 1] = son[k:j]
        son[k] = i
        i += 2
        j += 1

    for n in range(_T):
        k = son[n]
        prnt[k] = n
        if k < _T:
            prnt[k + 1] = n


def decompress(data: bytes, max_out: int = MAX_DECOMPRESSED_SIZE) -> bytes:
    """Decode the LZHUF stream that follows a "td" header (the bytes after the 12-byte header).
    Returns what an uncompressed "TD" image holds after its header, plus the same trailing bytes the
    C++ decoder emits for the zero bits that finish the last symbol."""
    data = bytes(data)
    total_bits = len(data) * 8

    padded = data + bytes(_PAD)
    bits = bytearray(b"".join([_BIT_TABLE[b] for b in padded]))
    limit = len(bits) - 256  # past this, append more zero padding

    # Huffman tree (startHuff); freq[T] = 0xFFFF is the sentinel that stops the swap search in update()
    freq = [0] * (_T + 1)
    prnt = [0] * (_T + _N_CHAR)
    son = [0] * _T
    for i in range(_N_CHAR):
        freq[i] = 1
        son[i] = i + _T
        prnt[i + _T] = i
    i = 0
    j = _N_CHAR
    while j <= _R:
        freq[j] = freq[i] + freq[i + 1]
        son[j] = i
        prnt[i] = prnt[i + 1] = j
        i += 2
        j += 1
    freq[_T] = 0xFFFF
    prnt[_R] = 0

    # The ring buffer is not kept: the output is preceded by the initial ring contents in ring order starting
    # at r = N - F (60 zeros, then 4036 spaces), so a match at distance d copies from out[len - d]
    out = bytearray(bytes(_F) + b" " * (_N - _F))
    append = out.append
    pos = 0  # bits consumed
    out_len = 0  # bytes decoded (len(out) - N)
    d_code = _D_CODE
    d_len = _D_LEN
    T = _T
    R = _R

    # hasInput(): C++ keeps a byte pointer and a bit count; "pointer < len or bits left in the current byte"
    # equals "fewer than all input bits consumed, or the last loaded byte is not used up yet"
    while (pos < total_bits or (pos & 7)) and out_len < max_out:
        if pos > limit:
            padded += bytes(_PAD)
            bits += bytes(_PAD * 8)
            limit = len(bits) - 256

        # decodeChar()
        c = son[R]
        while c < T:
            c = son[c + bits[pos]]
            pos += 1
        c -= T

        # update(c)
        if freq[R] == _MAX_FREQ:
            _reconstruct(freq, son, prnt)
        n = prnt[c + T]
        while True:
            k = freq[n] + 1
            freq[n] = k
            l = n + 1
            if k > freq[l]:
                l += 1
                while k > freq[l]:
                    l += 1
                l -= 1
                freq[n] = freq[l]
                freq[l] = k

                a = son[n]
                prnt[a] = l
                if a < T:
                    prnt[a + 1] = l
                b = son[l]
                son[l] = a
                prnt[b] = n
                if b < T:
                    prnt[b + 1] = n
                son[n] = b
                n = l
            n = prnt[n]
            if n == 0:
                break

        if c < 256:
            append(c)
            out_len += 1
            continue

        # decodePosition(): 8 bits index the prefix tables, then dLen - 2 more bits complete the low 6
        byte_at = pos >> 3
        shift = pos & 7
        v = (((padded[byte_at] << 8) | padded[byte_at + 1]) >> (8 - shift)) & 0xFF
        pos += 8
        upper = d_code[v] << 6
        for _ in range(d_len[v] - 2):
            v = (v << 1) | bits[pos]
            pos += 1
        position = upper | (v & 0x3F)

        length = c - 255 + _THRESHOLD
        if length > max_out - out_len:
            length = max_out - out_len
        start = len(out) - position - 1
        if position + 1 >= length:
            out += out[start : start + length]
        else:  # overlapping match repeats the bytes it has just produced
            for k in range(start, start + length):
                append(out[k])
        out_len += length

    return bytes(out[_N:])


def body_length(body: bytes, has_comment: bool) -> int:
    """Length of a TD0 body (comment block, tracks, end marker) as LoaderTD0::parseBody walks it.
    has_comment: bit 7 of the header's stepping byte. Raises ValueError where the C++ loader rejects the
    image; a body that ends cleanly at a track boundary without the 0xFF marker is accepted (C++ only warns)
    and its full length is returned."""
    size = len(body)
    offset = 0
    if has_comment:
        if offset + _COMMENT_HEADER_SIZE > size:
            raise ValueError("TD0 stream is truncated inside the comment block header")
        comment_length = body[2] | (body[3] << 8)
        if offset + _COMMENT_HEADER_SIZE + comment_length > size:
            raise ValueError("TD0 stream is truncated inside the comment text")
        offset += _COMMENT_HEADER_SIZE + comment_length

    while True:
        if offset >= size:
            return size
        if body[offset] == _END_OF_IMAGE:
            return offset + 1
        if offset + _TRACK_HEADER_SIZE > size:
            raise ValueError(f"TD0 stream is truncated inside a track header at offset {offset}")
        sector_count = body[offset]
        cylinder = body[offset + 1]
        head = body[offset + 2] & 0x7F
        offset += _TRACK_HEADER_SIZE
        for _ in range(sector_count):
            if offset + _SECTOR_HEADER_SIZE > size:
                raise ValueError(f"TD0 stream is truncated inside the sector list of cylinder {cylinder} head {head}")
            number = body[offset + 2]
            flags = body[offset + 4]
            offset += _SECTOR_HEADER_SIZE
            if flags & _FLAGS_NO_DATA_BLOCK:
                continue
            if offset + 2 > size:
                raise ValueError(f"TD0 stream is truncated at the data block of cylinder {cylinder} head {head} sector {number}")
            block_length = body[offset] | (body[offset + 1] << 8)
            offset += 2
            if block_length == 0 or offset + block_length > size:
                raise ValueError(f"TD0 stream is truncated inside the data block of cylinder {cylinder} head {head} sector {number}")
            offset += block_length


def decompress_checked(data: bytes, has_comment: bool, max_out: int = MAX_DECOMPRESSED_SIZE) -> bytes:
    """decompress() and then body_length() on the result: raises ValueError for a stream the C++ loader
    would reject. Returns the decoded body cut after the end marker (the trailing decoder junk dropped)."""
    out = decompress(data, max_out)
    return out[: body_length(out, has_comment)]
