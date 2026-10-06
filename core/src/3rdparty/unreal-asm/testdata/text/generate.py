#!/usr/bin/env python3
"""Generates the text-codec corpus (run from this folder: python3 generate.py). One Z80 source with Russian
comments, written in every code page and with every line-end style the text codec must keep byte-exact; the
expected UTF-8 text is in source.utf8.txt."""

SOURCE = """; Пример исходника: вывод строки на экран
        ORG #8000
START   LD HL,TEXT          ; адрес строки
        CALL PRINT          ; печать до нуля
        RET
PRINT   LD A,(HL)
        OR A
        RET Z
        RST #10             ; вывод символа через ПЗУ
        INC HL
        JR PRINT
TEXT    DB "Привет, Спектрум!",0
; конец файла"""

lines = SOURCE.split("\n")
open("source.utf8.txt", "w", encoding="utf-8", newline="").write("\n".join(lines) + "\n")
for name, codec in [("cp866", "cp866"), ("koi8r", "koi8_r"), ("cp1251", "cp1251"), ("utf8", "utf-8")]:
    for end_name, end in [("lf", "\n"), ("crlf", "\r\n"), ("cr", "\r")]:
        data = end.join(lines).encode(codec) + end.encode()
        open(f"source-{name}-{end_name}.asm", "wb").write(data)
# Layout cases: a UTF-8 byte-order mark, no final line break, mixed line ends, an invalid UTF-8 byte, empty lines
open("bom-utf8.asm", "wb").write(b"\xef\xbb\xbf" + "\n".join(lines).encode("utf-8") + b"\n")
open("no-final-break.asm", "wb").write("\n".join(lines).encode("cp866"))
open("mixed-ends.asm", "wb").write(b"\tLD A,1\r\n\tLD B,2\n\tLD C,3\r\tRET\n")
open("invalid-utf8.asm", "wb").write(b"\tDB \"\xff\xfe\"\n\tRET\n")
open("empty-lines.asm", "wb").write(b"\n\n\tNOP\n\n")
open("empty.asm", "wb").write(b"")
print("written")
