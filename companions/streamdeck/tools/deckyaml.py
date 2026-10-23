#!/usr/bin/env python3
"""A small YAML subset for the Stream Deck scenario pipeline (deckflow.py), Python standard library only.

Reads what the pipeline needs, with the line of every entry (errors name file:line, the spec patcher edits lines):
block mappings and sequences, flow collections ([a, b], {k: v}, spanning lines until they close), plain, single- and
double-quoted scalars, comments. Not supported: anchors, aliases, tags, multi-line plain or block scalars (| >),
complex keys, several documents.

Writes two styles:
  dump_fk(value)   the block style of fkYAML 0.5 (the companion's YAML library: unreal-streamdeck, Stream Deck
                   Studio's Save), keys sorted as nlohmann::json sorts them, so a file the studio saves again differs
                   only where something changed
  flow(value)      one-line flow style for the hand-edited spec ({at: [0, 1], action: run.pause_toggle})
"""
import json
import re


class YamlError(Exception):
    def __init__(self, line, message):
        super().__init__(f"line {line}: {message}")
        self.line = line
        self.message = message


class Loc:
    """Where a value is: first and last line (1-based, inclusive); a mapping's entries by key; a sequence's items."""
    __slots__ = ("line", "end", "keys", "items", "key_line")

    def __init__(self, line, end=None):
        self.line = line
        self.end = end if end is not None else line
        self.keys = {}
        self.items = []
        self.key_line = line

    def __repr__(self):
        return f"Loc({self.line}..{self.end})"


# ---------------------------------------------------------------- scalars

_INT = re.compile(r"^[-+]?[0-9]+$")
_HEX = re.compile(r"^0x[0-9a-fA-F]+$")
_OCT = re.compile(r"^0o[0-7]+$")
_FLOAT = re.compile(r"^[-+]?(\.[0-9]+|[0-9]+(\.[0-9]*)?)([eE][-+]?[0-9]+)?$")
_SPECIAL_FLOAT = {".inf": float("inf"), ".Inf": float("inf"), ".INF": float("inf"), "+.inf": float("inf"),
                  "-.inf": float("-inf"), "-.Inf": float("-inf"), "-.INF": float("-inf"), ".nan": float("nan"),
                  ".NaN": float("nan"), ".NAN": float("nan")}
_NULL = {"", "~", "null", "Null", "NULL"}
_TRUE = {"true", "True", "TRUE"}
_FALSE = {"false", "False", "FALSE"}


def plain_value(s):
    """A plain scalar's value (YAML 1.2 core schema)."""
    if s in _NULL:
        return None
    if s in _TRUE:
        return True
    if s in _FALSE:
        return False
    if _INT.match(s):
        return int(s)
    if _HEX.match(s):
        return int(s, 16)
    if _OCT.match(s):
        return int(s[2:], 8)
    if _FLOAT.match(s) and any(c.isdigit() for c in s):
        return float(s)
    if s in _SPECIAL_FLOAT:
        return _SPECIAL_FLOAT[s]
    return s


def looks_typed(s):
    """Would this text read as something other than a string?"""
    return not isinstance(plain_value(s), str)


_ESC = {"0": "\0", "a": "\a", "b": "\b", "t": "\t", "\t": "\t", "n": "\n", "v": "\v", "f": "\f", "r": "\r",
        "e": "\x1b", " ": " ", '"': '"', "/": "/", "\\": "\\", "N": "\x85", "_": "\xa0", "L": " ",
        "P": " "}


def unquote_double(body, line):
    out, i = [], 0
    while i < len(body):
        c = body[i]
        if c == "\\":
            if i + 1 >= len(body):
                raise YamlError(line, "a backslash ends the string")
            e = body[i + 1]
            if e in _ESC:
                out.append(_ESC[e])
                i += 2
                continue
            n = {"x": 2, "u": 4, "U": 8}.get(e)
            if n is None:
                raise YamlError(line, f"unknown escape \\{e}")
            try:
                out.append(chr(int(body[i + 2:i + 2 + n], 16)))
            except ValueError:
                raise YamlError(line, f"bad escape \\{e}{body[i + 2:i + 2 + n]}") from None
            i += 2 + n
            continue
        out.append(c)
        i += 1
    return "".join(out)


# ---------------------------------------------------------------- reading

def strip_comment(text):
    """The line without its comment (a # at the start or after a space, outside quotes)."""
    q = None
    i = 0
    while i < len(text):
        c = text[i]
        if q == '"':
            if c == "\\":
                i += 2
                continue
            if c == '"':
                q = None
        elif q == "'":
            if c == "'":
                if i + 1 < len(text) and text[i + 1] == "'":
                    i += 2
                    continue
                q = None
        else:
            if c in "\"'" and (i == 0 or text[i - 1] in " \t[{,:-?"):
                q = c
            elif c == "#" and (i == 0 or text[i - 1] in " \t"):
                return text[:i].rstrip()
        i += 1
    return text.rstrip()


def comment_of(text):
    """The comment of a line (from its #, with the spaces before it), or ''."""
    body = strip_comment(text)
    rest = text[len(body):]
    return rest.rstrip("\n") if rest.strip().startswith("#") else ""


class _Line:
    __slots__ = ("no", "indent", "text")

    def __init__(self, no, indent, text):
        self.no, self.indent, self.text = no, indent, text


class _Flow:
    """A flow collection or scalar in one string (lines joined), positions mapped back to lines."""

    def __init__(self, text, lines_at):
        self.s = text
        self.i = 0
        self.lines_at = lines_at  # list of (offset, line no)

    def line(self, pos=None):
        pos = self.i if pos is None else pos
        no = self.lines_at[0][1]
        for off, n in self.lines_at:
            if off <= pos:
                no = n
        return no

    def ws(self):
        while self.i < len(self.s) and self.s[self.i] in " \t\n":
            self.i += 1

    def peek(self):
        self.ws()
        return self.s[self.i] if self.i < len(self.s) else ""

    def fail(self, msg):
        raise YamlError(self.line(), msg)

    def value(self):
        c = self.peek()
        start = self.line()
        if c == "[":
            self.i += 1
            out, loc = [], Loc(start)
            if self.peek() == "]":
                self.i += 1
                loc.end = self.line(self.i - 1)
                return out, loc
            while True:
                if self.peek() in ",]":
                    self.fail("an empty entry in a flow sequence")
                v, vl = self.value()
                if self.peek() == ":":  # a single-pair mapping inside a sequence: [a: 1]
                    self.fail("a key: value pair in a flow sequence is not supported; use {k: v}")
                out.append(v)
                loc.items.append(vl)
                c = self.peek()
                if c == ",":
                    self.i += 1
                    if self.peek() == "]":
                        self.i += 1
                        break
                    continue
                if c == "]":
                    self.i += 1
                    break
                self.fail("expected ',' or ']' in a flow sequence" + (" (a line break inside?)" if not c else ""))
            loc.end = self.line(self.i - 1)
            return out, loc
        if c == "{":
            self.i += 1
            out, loc = {}, Loc(start)
            if self.peek() == "}":
                self.i += 1
                loc.end = self.line(self.i - 1)
                return out, loc
            while True:
                kline = self.line()
                k, _ = self.scalar(key=True)
                if not isinstance(k, (str, int, float, bool)) or k is None:
                    self.fail("a mapping key must be a scalar")
                if self.peek() != ":":
                    self.fail(f"expected ':' after the key '{k}'")
                self.i += 1
                if self.peek() in ",}":
                    v, vl = None, Loc(kline)
                else:
                    v, vl = self.value()
                if k in out:
                    raise YamlError(kline, f"duplicate key '{k}'")
                out[k] = v
                vl.key_line = kline
                loc.keys[k] = vl
                c = self.peek()
                if c == ",":
                    self.i += 1
                    if self.peek() == "}":
                        self.i += 1
                        break
                    continue
                if c == "}":
                    self.i += 1
                    break
                self.fail("expected ',' or '}' in a flow mapping" + (" (not closed?)" if not c else ""))
            loc.end = self.line(self.i - 1)
            return out, loc
        return self.scalar()

    def scalar(self, key=False):
        self.ws()
        start = self.line()
        if self.i >= len(self.s):
            self.fail("a value is missing")
        c = self.s[self.i]
        if c == '"':
            j = self.i + 1
            while j < len(self.s) and self.s[j] != '"':
                j += 2 if self.s[j] == "\\" else 1
            if j >= len(self.s):
                self.fail("a double-quoted string is not closed")
            v = unquote_double(self.s[self.i + 1:j], start)
            self.i = j + 1
            return v, Loc(start)
        if c == "'":
            j = self.i + 1
            buf = []
            while True:
                if j >= len(self.s):
                    self.fail("a single-quoted string is not closed")
                if self.s[j] == "'":
                    if j + 1 < len(self.s) and self.s[j + 1] == "'":
                        buf.append("'")
                        j += 2
                        continue
                    break
                buf.append(self.s[j])
                j += 1
            self.i = j + 1
            return "".join(buf), Loc(start)
        if c in "&*!|>%@`":
            self.fail(f"'{c}' (anchors, aliases, tags, block scalars) is not supported")
        j = self.i
        while j < len(self.s):
            ch = self.s[j]
            if ch in ",[]{}\n":
                break
            if ch == ":" and (j + 1 >= len(self.s) or self.s[j + 1] in " ,[]{}\n"):
                break
            if ch == "#" and j > 0 and self.s[j - 1] in " \t":
                break
            j += 1
        text = self.s[self.i:j].strip()
        self.i = j
        if text == "":
            self.fail("a value is missing")
        return (text if key else plain_value(text)), Loc(start)


def _flow_value(lines, k, first_text, first_no, indent=-1):
    """Parse a flow value starting on lines[k] (its text from the value on); returns (value, loc, next k).
    A collection that does not close goes on over the lines indented deeper than its entry."""
    text = first_text
    offsets = [(0, first_no)]
    depth = _depth(text)
    j = k + 1
    while depth > 0 and j < len(lines):
        if lines[j].indent <= indent:
            raise YamlError(first_no, "a flow collection opened here is not closed")
        offsets.append((len(text) + 1, lines[j].no))
        text += "\n" + lines[j].text.strip()
        depth = _depth(text)
        j += 1
    f = _Flow(text, offsets)
    v, loc = f.value()
    f.ws()
    if f.i < len(f.s):
        raise YamlError(f.line(), f"unexpected text after the value: '{f.s[f.i:].strip()}'")
    loc.line = first_no
    loc.end = lines[j - 1].no
    return v, loc, j


def _depth(text):
    d, q, i = 0, None, 0
    while i < len(text):
        c = text[i]
        if q == '"':
            if c == "\\":
                i += 2
                continue
            if c == '"':
                q = None
        elif q == "'":
            if c == "'":
                q = None
        elif c in "\"'":
            q = c
        elif c in "[{":
            d += 1
        elif c in "]}":
            d -= 1
        i += 1
    return d


_KEY = re.compile(r"""^((?:"(?:[^"\\]|\\.)*")|(?:'(?:[^']|'')*')|(?:[^\s"'#\-?:,\[\]{}][^:#]*?|-[^\s:][^:#]*?))\s*:(?:\s+|$)""")


def _split_key(text, no):
    m = _KEY.match(text)
    if not m:
        return None
    raw = m.group(1).strip()
    if raw.startswith('"'):
        key = unquote_double(raw[1:-1], no)
    elif raw.startswith("'"):
        key = raw[1:-1].replace("''", "'")
    else:
        key = raw
        if isinstance(plain_value(raw), int) and not isinstance(plain_value(raw), bool):
            key = plain_value(raw)
    return key, text[m.end():]


def load(text, with_locs=False):
    """Parse a YAML text; with_locs: also the Loc tree."""
    lines = []
    for no, raw in enumerate(text.splitlines(), 1):
        if "\t" in raw[:len(raw) - len(raw.lstrip())]:
            raise YamlError(no, "a tab in the indentation")
        body = strip_comment(raw)
        if not body.strip() or body.strip() in ("---", "..."):
            continue
        lines.append(_Line(no, len(body) - len(body.lstrip()), body.strip()))
    if not lines:
        return (None, Loc(1)) if with_locs else None
    v, loc, k = _block(lines, 0, lines[0].indent)
    if k < len(lines):
        raise YamlError(lines[k].no, "unexpected indentation")
    return (v, loc) if with_locs else v


def _block(lines, k, indent):
    first = lines[k]
    if first.text.startswith("- ") or first.text == "-":
        return _seq(lines, k, indent)
    if _split_key(first.text, first.no) is None:
        v, loc, k2 = _flow_value(lines, k, first.text, first.no)
        return v, loc, k2
    return _map(lines, k, indent)


def _value_after(lines, k, rest, no, indent):
    """The value of an entry whose text after ':' or '- ' is rest: inline, or the block below (deeper)."""
    if rest.strip() == "":
        if k + 1 < len(lines) and lines[k + 1].indent > indent:
            v, loc, k2 = _block(lines, k + 1, lines[k + 1].indent)
            loc.line = no
            return v, loc, k2
        if k + 1 < len(lines) and lines[k + 1].indent == indent and (lines[k + 1].text.startswith("- ")
                                                                    or lines[k + 1].text == "-"):
            # a sequence at the same indentation as its key (allowed for mapping values)
            v, loc, k2 = _seq(lines, k + 1, indent)
            loc.line = no
            return v, loc, k2
        return None, Loc(no), k + 1
    rest = rest.strip()
    if rest[0] in "[{\"'":
        return _flow_value(lines, k, rest, no, indent)
    if rest[0] in "&*!|>%@`":
        raise YamlError(no, f"'{rest[0]}' (anchors, aliases, tags, block scalars) is not supported")
    return plain_value(rest), Loc(no), k + 1


def _map(lines, k, indent):
    out, loc = {}, Loc(lines[k].no)
    while k < len(lines) and lines[k].indent == indent:
        ln = lines[k]
        if ln.text.startswith("- "):
            break
        kv = _split_key(ln.text, ln.no)
        if kv is None:
            raise YamlError(ln.no, f"expected 'key: value', got '{ln.text}'")
        key, rest = kv
        if key in out:
            raise YamlError(ln.no, f"duplicate key '{key}'")
        v, vl, k = _value_after(lines, k, rest, ln.no, indent)
        vl.key_line = ln.no
        vl.line = ln.no
        out[key] = v
        loc.keys[key] = vl
        loc.end = vl.end
    if k < len(lines) and lines[k].indent > indent:
        raise YamlError(lines[k].no, "unexpected indentation")
    return out, loc, k


def _seq(lines, k, indent):
    out, loc = [], Loc(lines[k].no)
    while k < len(lines) and lines[k].indent == indent and (lines[k].text.startswith("- ") or lines[k].text == "-"):
        ln = lines[k]
        rest = ln.text[1:]
        inner = rest.lstrip()
        if inner and _split_key(inner, ln.no) is not None and not inner.startswith(("[", "{")):
            # "- key: value" starts a mapping indented at the text after the dash
            sub_indent = indent + 1 + (len(rest) - len(inner))
            lines[k] = _Line(ln.no, sub_indent, inner)
            v, vl, k = _map(lines, k, sub_indent)
        else:
            v, vl, k = _value_after(lines, k, inner, ln.no, indent)
        out.append(v)
        loc.items.append(vl)
        loc.end = vl.end
    return out, loc, k


# ---------------------------------------------------------------- writing

def _fk_plain_ok(s):
    """fkYAML's is_valid_plain_scalar plus its scan for other types."""
    if not s or looks_typed(s):
        return False
    if any(c in s for c in " \t\n\r,[]{}"):
        return False
    c = s[0]
    if c in "-?:":
        return len(s) > 1 and s[1] != " "
    if c in "#&*!|>'\"%@` ":
        return False
    return s[-1] != " "


def _fk_escape(s):
    out, escaped = [], False
    named = {"\a": "\\a", "\b": "\\b", "\t": "\\t", "\n": "\\n", "\v": "\\v", "\f": "\\f", "\r": "\\r",
             "\x1b": "\\e", '"': '\\"', "\\": "\\\\", "\x85": "\\N", "\xa0": "\\_", " ": "\\L",
             " ": "\\P"}
    for c in s:
        if c in named:
            out.append(named[c])
            escaped = True
        elif ord(c) < 0x20:
            out.append("\\u%04X" % ord(c))
            escaped = True
        else:
            out.append(c)
    return "".join(out), escaped


def _fk_scalar(v):
    if v is None:
        return "null"
    if v is True:
        return "true"
    if v is False:
        return "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        if v != v:
            return ".nan"
        if v in (float("inf"), float("-inf")):
            return ".inf" if v > 0 else "-.inf"
        r = repr(v)
        return r
    s, escaped = _fk_escape(str(v))
    if escaped or not _fk_plain_ok(str(v)):
        return '"' + s + '"'
    return s


def _json_key_order(d):
    return sorted(d.keys(), key=lambda k: str(k).encode("utf-8"))


def dump_fk(value):
    """fkYAML 0.5's block style (serialize_node): what unreal-streamdeck and Stream Deck Studio write."""
    out = []

    def cur_indent():
        s = "".join(out)
        i = s.rfind("\n")
        return len(s) - i - 1 if i >= 0 else len(s)

    def indent(n):
        if n:
            out.append(" " * max(0, n - cur_indent()))

    def node(v, ind):
        if isinstance(v, list):
            if not v:
                out.append("[]\n")
                return
            for item in v:
                indent(ind)
                out.append("-")
                if not isinstance(item, (list, dict)):
                    out.append(" " + _fk_scalar(item) + "\n")
                elif item:
                    out.append("\n")
                    node(item, ind + 2)
                else:
                    out.append(" []\n" if isinstance(item, list) else " {}\n")
            return
        if isinstance(v, dict):
            if not v:
                out.append("{}\n")
                return
            for k in _json_key_order(v):
                indent(ind)
                out.append(_fk_scalar(str(k)) + ":")
                x = v[k]
                if not isinstance(x, (list, dict)):
                    out.append(" " + _fk_scalar(x) + "\n")
                    continue
                out.append(" " if not x else "\n")
                node(x, ind + 2)
            return
        out.append(_fk_scalar(v))

    node(value, 0)
    text = "".join(out)
    return text if text.endswith("\n") else text + "\n"


_FLOW_PLAIN = re.compile(r"^[A-Za-z0-9_$./+][A-Za-z0-9_$./+:=@()-]*$")


def flow_scalar(v):
    if v is None:
        return "null"
    if v is True:
        return "true"
    if v is False:
        return "false"
    if isinstance(v, (int, float)):
        return repr(v) if isinstance(v, float) else str(v)
    s = str(v)
    if (_FLOW_PLAIN.match(s) and not looks_typed(s) and not s.endswith(":") and ": " not in s
            and "#" not in s):
        return s
    if re.match(r"^[A-Za-z0-9_$.][A-Za-z0-9_$./+ :=()-]*[A-Za-z0-9_$./)]$", s) and ": " not in s \
            and " #" not in s and not looks_typed(s) and not s.endswith(":"):
        return s  # words with single spaces read back as one plain scalar
    return json.dumps(s, ensure_ascii=False)


def flow(v):
    """One-line flow style."""
    if isinstance(v, dict):
        return "{" + ", ".join(f"{flow_key(k)}: {flow(x)}" for k, x in v.items()) + "}"
    if isinstance(v, list):
        return "[" + ", ".join(flow(x) for x in v) + "]"
    return flow_scalar(v)


def flow_key(k):
    if isinstance(k, int) and not isinstance(k, bool):
        return str(k)
    s = str(k)
    if re.match(r"^[A-Za-z_$][A-Za-z0-9_$./-]*$", s) and not looks_typed(s):
        return s
    return json.dumps(s, ensure_ascii=False)
