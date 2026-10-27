#!/usr/bin/env python3
"""POC 028: builds the simulation tree of the ZX Next core in GHDL from the official RTL (read only) with the minimum of changes.

    patch_rtl.py <rtl src dir (cores/zxnext/src)> <out dir>

The RTL's licence is the GPL of the Next project: the patched copies stay in the out dir (scratch/), only this script is committed.
Changes (all listed in the measurement report):
  - the generic boot ROM's UNISIM use removed (bootrom*.vhd)
  - dpram2: the VHDL-87 file declaration, and power-on pattern contents for bank 5 and the palettes (a measurable picture)
  - fifop: to_unsigned(-1, n) (a bound check failure in GHDL) written as the all-ones vector
  - layer2: layer2_req_t initial value (the FPGA powers up with 0)
  - every other signal gets a zero initial value (the FPGA's registers power up zero; GHDL starts them U)
  - models of the coregen RAMs sdpram_16_9 / sdpram_64_9 / sdpram_128_8 (tilemap line buffer, UART FIFOs, sprite attributes)
  - sprites: the attribute RAMs (sdpram_128_8_n), the line buffers (spram_320_9) and the pattern RAM (sdpbram_16k_8) modelled
  - zxnext: test hooks (palette write too), (package tbh): forced registers at the end of the NextREG process, the Z80 held in WAIT, probes of hc / vc
"""
import os
import re
import shutil
import sys

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
EXCLUDE = re.compile(r"zxnext_top|/pll/|flashboot|uart_old|_old|/hdmi/")
files = []
for root, _, names in os.walk(src):
    for n in names:
        p = os.path.join(root, n)
        if n.endswith(".vhd") and not EXCLUDE.search(p):
            files.append(p)


def init_signals(text):
    text = re.sub(r"(\bsignal\s+[\w\s,]+:\s*(?:std_logic|std_ulogic)\s*)(?=;)", lambda m: m.group(1) + " := '0'", text, flags=re.I)
    text = re.sub(r"(\bsignal\s+[\w\s,]+:\s*(?:std_logic_vector|unsigned|signed)\s*\([^;:=]*\))\s*;",
                  lambda m: m.group(1) + " := (others => '0');", text, flags=re.I)
    return text


PATTERN = """      if RamFileName = "init/none.bin.txt" then
         if addr_width_g = 14 and data_width_g = 8 then
            -- bank 5 (ULA screen): bitmap rows alternate #F0 / #0F, tile 0's rows alternate; attribute per column: ink = col mod 8,
            -- paper = (col + 3) mod 8
            for I in ram_t'range loop
               if I < 6144 then
                  if I >= 3072 and I < 3104 then
                     if ((I / 4) mod 2) = 0 then
                        RAM(I) := to_bitvector(std_logic_vector(to_unsigned(240, data_width_g)));
                     else
                        RAM(I) := to_bitvector(std_logic_vector(to_unsigned(15, data_width_g)));
                     end if;
                  elsif ((I / 256) mod 2) = 0 then
                     RAM(I) := to_bitvector(std_logic_vector(to_unsigned(240, data_width_g)));
                  else
                     RAM(I) := to_bitvector(std_logic_vector(to_unsigned(15, data_width_g)));
                  end if;
               elsif I < 6912 then
                  RAM(I) := to_bitvector(std_logic_vector(to_unsigned(((I mod 32) mod 8) + 8 * (((I mod 32) + 3) mod 8), data_width_g)));
               else
                  RAM(I) := to_bitvector(std_logic_vector(to_unsigned(0, data_width_g)));
               end if;
            end loop;
            return RAM;
         elsif addr_width_g = 10 and data_width_g = 16 then
            -- palettes: the entry's colour is its index (distinct colours)
            for I in ram_t'range loop
               RAM(I) := to_bitvector(std_logic_vector(to_unsigned(I mod 512, data_width_g)));
            end loop;
            return RAM;
         end if;
         return RAM_0;"""

if os.environ.get("POC_PATTERN") == "lores":
    # LoRes reads bank 5 as 128 bytes a row: make every byte differ from its neighbours (the ULA picture changes with it)
    PATTERN = PATTERN.replace("               if I < 6144 then\n",
                              "               if I < 6144 then\n                  RAM(I) := to_bitvector(std_logic_vector(to_unsigned(((I mod 128) * 5 + (I / 128) * 29) mod 256, data_width_g)));\n               elsif I < 6144 then\n", 1)
    assert "mod 128" in PATTERN

if os.environ.get("POC_PATTERN") == "dense":
    # every ULA attribute the same (ink 2, paper 0: dense palette index 2 / 16), a tilemap whose entries alternate tile 0 / 1 and attribute bit 0 every second entry, and a
    # second tile (#77), so that the tilemap control bits and the palette value writes have pixels to change
    a = "to_unsigned(((I mod 32) mod 8) + 8 * (((I mod 32) + 3) mod 8), data_width_g)"
    assert a in PATTERN
    PATTERN = PATTERN.replace(a, "to_unsigned(2, data_width_g)", 1)
    b = "                  elsif ((I / 256) mod 2) = 0 then\n"
    assert b in PATTERN
    PATTERN = PATTERN.replace(b, "                  elsif I >= 3104 and I < 3136 then\n                     RAM(I) := to_bitvector(std_logic_vector(to_unsigned(119, data_width_g)));\n" + b, 1)
    c = "               else\n                  RAM(I) := to_bitvector(std_logic_vector(to_unsigned(0, data_width_g)));\n"
    assert c in PATTERN
    PATTERN = PATTERN.replace(c, "               elsif I >= 11264 then\n                  RAM(I) := to_bitvector(std_logic_vector(to_unsigned((((I - 11264) / 2) mod 2) * (1 - (I mod 2)) + (((I - 11264) / 4) mod 2) * (I mod 2), data_width_g)));\n" + c, 1)

# (signal, value byte index, high bit): the byte index is the t_en / t_val slot of tbh
OVERRIDES = [("nr_68_ula_en", 0, 0), ("nr_15_sprite_en", 1, 0), ("nr_15_lores_en", 2, 0), ("nr_6b_tm_en", 3, 0),
             ("nr_26_ula_scrollx", 5, 7), ("nr_27_ula_scrolly", 6, 7), ("nr_16_layer2_scrollx", 7, 7),
             ("nr_1a_ula_clip_x1", 8, 7), ("nr_1a_ula_clip_x2", 9, 7), ("nr_18_layer2_clip_x1", 10, 7), ("nr_18_layer2_clip_x2", 11, 7),
             ("nr_4a_fallback_rgb", 12, 7), ("nr_15_layer_priority", 13, 2), ("nr_14_global_transparent_rgb", 14, 7),
             ("nr_17_layer2_scrolly", 15, 7), ("nr_18_layer2_clip_y1", 16, 7), ("nr_18_layer2_clip_y2", 17, 7),
             ("nr_71_layer2_scrollx_msb", 18, 0), ("nr_70_layer2_palette_offset", 19, 3), ("nr_1a_ula_clip_y1", 20, 7),
             ("nr_1a_ula_clip_y2", 21, 7), ("nr_31_tm_scrolly", 24, 7), ("nr_1b_tm_clip_x1", 25, 7), ("nr_1b_tm_clip_x2", 26, 7),
             ("nr_1b_tm_clip_y1", 27, 7), ("nr_1b_tm_clip_y2", 28, 7), ("nr_68_ula_fine_scroll_x", 29, 0),
             ("nr_43_active_ula_palette", 30, 0), ("nr_43_active_layer2_palette", 31, 0), ("nr_6b_tm_control", 32, 6),
             ("nr_32_lores_scrollx", 33, 7), ("nr_33_lores_scrolly", 34, 7), ("nr_43_ulanext_en", 35, 0),
             ("nr_42_ulanext_format", 36, 7), ("nr_4c_tm_transparent_index", 37, 3),
             ("nr_19_sprite_clip_x1", 38, 7), ("nr_19_sprite_clip_x2", 39, 7), ("nr_19_sprite_clip_y1", 40, 7),
             ("nr_19_sprite_clip_y2", 41, 7), ("nr_43_palette_write_select", 42, 2)]


def ram_model(name, aw, dw):
    return f"""library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity {name} is
   port (
      DPRA : in std_logic_vector({aw}-1 downto 0) := (others => '0');
      CLK  : in std_logic;
      WE   : in std_logic;
      DPO  : out std_logic_vector({dw}-1 downto 0);
      A    : in std_logic_vector({aw}-1 downto 0) := (others => '0');
      D    : in std_logic_vector({dw}-1 downto 0) := (others => '0')
   );
end entity;
architecture sim of {name} is
   type ram_t is array (0 to 2**{aw}-1) of std_logic_vector({dw}-1 downto 0);
   signal ram : ram_t := (others => (others => '0'));
begin
   process (CLK)
   begin
      if rising_edge(CLK) then
         if WE = '1' then
            ram(to_integer(unsigned(A))) <= D;
         end if;
      end if;
   end process;
   DPO <= ram(to_integer(unsigned(DPRA)));
end architecture;
"""


def sprite_ram_model(name, attr):
    # sprite attribute RAM: every one of the 128 sprites visible, 16x16 tiles of a 16 x 8 grid, one pattern, palette offset 8
    init = {0: "std_logic_vector(to_unsigned(32 + (i mod 16) * 16, 8))",
            1: "std_logic_vector(to_unsigned(32 + (i / 16) * 16, 8))",
            2: "x\"80\"", 3: "x\"80\"", 4: "x\"00\""}[attr]
    return f"""library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity {name} is
   port (
      DPRA : in std_logic_vector(6 downto 0) := (others => '0');
      CLK  : in std_logic;
      WE   : in std_logic;
      DPO  : out std_logic_vector(7 downto 0);
      A    : in std_logic_vector(6 downto 0) := (others => '0');
      D    : in std_logic_vector(7 downto 0) := (others => '0')
   );
end entity;
architecture sim of {name} is
   type ram_t is array (0 to 127) of std_logic_vector(7 downto 0);
   function init_f return ram_t is
      variable r : ram_t;
   begin
      for i in r'range loop
         r(i) := {init};
      end loop;
      return r;
   end function;
   signal ram : ram_t := init_f;
begin
   process (CLK)
   begin
      if rising_edge(CLK) then
         if WE = '1' then ram(to_integer(unsigned(A))) <= D; end if;
      end if;
   end process;
   DPO <= ram(to_integer(unsigned(DPRA)));
end architecture;
"""


SPRITE_MODELS = """library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity spram_320_9 is
   port (
      CLK : in std_logic;
      WE  : in std_logic;
      SPO : out std_logic_vector(8 downto 0);
      A   : in std_logic_vector(8 downto 0) := (others => '0');
      D   : in std_logic_vector(8 downto 0) := (others => '0')
   );
end entity;
architecture sim of spram_320_9 is
   type ram_t is array (0 to 511) of std_logic_vector(8 downto 0);
   signal ram : ram_t := (others => (others => '0'));
begin
   process (CLK)
   begin
      if rising_edge(CLK) then
         if WE = '1' then ram(to_integer(unsigned(A))) <= D; end if;
      end if;
   end process;
   SPO <= ram(to_integer(unsigned(A)));
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity sdpbram_16k_8 is
   port (
      WEA   : in std_logic_vector(0 downto 0);
      ADDRA : in std_logic_vector(13 downto 0);
      DINA  : in std_logic_vector(7 downto 0);
      CLKA  : in std_logic;
      ENB   : in std_logic;
      ADDRB : in std_logic_vector(13 downto 0);
      DOUTB : out std_logic_vector(7 downto 0);
      CLKB  : in std_logic
   );
end entity;
architecture sim of sdpbram_16k_8 is
   type ram_t is array (0 to 16383) of std_logic_vector(7 downto 0);
   -- one solid colour (index #21: not the transparent #E3) in every pattern
   signal ram : ram_t := (others => x"21");
begin
   process (CLKA)
   begin
      if rising_edge(CLKA) then
         if WEA(0) = '1' then ram(to_integer(unsigned(ADDRA))) <= DINA; end if;
      end if;
   end process;
   process (CLKB)
   begin
      if rising_edge(CLKB) then
         if ENB = '1' then DOUTB <= ram(to_integer(unsigned(ADDRB))); end if;
      end if;
   end process;
end architecture;
"""

listing = []
for f in sorted(files):
    base = os.path.basename(f)
    text = open(f).read()
    if base in ("bootrom.vhd", "bootrom_ab.vhd"):
        text = "\n".join(l for l in text.split("\n") if "unisim" not in l.lower())
    elif base == "dpram2.vhd":
        text = text.replace("file     RamFile     : text is in RamFileName;", "file     RamFile     : text open read_mode is RamFileName;")
        marker = '''      if RamFileName = "init/none.bin.txt" then
         return RAM_0;'''
        assert marker in text
        text = text.replace(marker, PATTERN, 1)
    elif base == "fifop.vhd":
        text = text.replace("std_logic_vector(to_unsigned(-1,stored'length-1))", "(stored'length-2 downto 0 => '1')")
        text = init_signals(text)
    elif base == "layer2.vhd":
        text = text.replace("   signal layer2_req_t           : std_logic;", "   signal layer2_req_t           : std_logic := '0';")
        text = init_signals(text)
    elif base == "zxnext.vhd":
        text = init_signals(text)
        text = text.replace("use work.Z80N_pack.all;", "use work.Z80N_pack.all;\nuse work.tbh.all;", 1)
        old = "z80_wait_n <= '0' when (ula_wait_n = '0')"
        assert old in text
        text = text.replace(old, "z80_wait_n <= '0' when (t_hold = '1') or (ula_wait_n = '0')", 1)
        tail = """            end case;

         end if;
      
      end if;
   end process;
"""
        assert text.count(tail) == 1
        lines = ["         -- POC 028 test hook: forced registers"]
        for sig, i, hi in OVERRIDES:
            if hi == 0:
                lines.append(f"         if t_en({i}) = '1' then {sig} <= t_val({i * 8}); end if;")
            else:
                lines.append(f"         if t_en({i}) = '1' then {sig} <= t_val({i * 8 + hi} downto {i * 8}); end if;")
        lines.append("         if t_en(22) = '1' then nr_30_tm_scrollx <= t_val(185 downto 184) & t_val(183 downto 176); end if;")
        text = text.replace(tail, """            end case;

         end if;

""" + "\n".join(lines) + """
      
      end if;
   end process;
""", 1)
        for old, new in (
                ("nr_palette_we <= '1' when nr_41_we = '1' or", "nr_palette_we <= '1' when t_pal_we = '1' or nr_41_we = '1' or"),
                ("nr_palette_value <= (nr_wr_dat &", "nr_palette_value <= t_pal_val when t_pal_we = '1' else (nr_wr_dat &"),
                ("nr_palette_index <= nr_43_palette_write_select(1) & nr_43_palette_write_select(2) & nr_palette_idx;",
                 "nr_palette_index <= nr_43_palette_write_select(1) & nr_43_palette_write_select(2) & nr_palette_idx when t_pal_we = '0'\n"
                 "      else nr_43_palette_write_select(1) & nr_43_palette_write_select(2) & t_pal_idx;")):
            assert text.count(old) == 1, old
            text = text.replace(old, new, 1)
        for old, new in (
                ("nr_wr_en <= copper_req or cpu_req;", "nr_wr_en <= copper_req or cpu_req or t_nr_we;"),
                ("nr_wr_reg <= copper_nr_reg when copper_req = '1' else cpu_nr_reg;",
                 "nr_wr_reg <= t_nr_reg when t_nr_we = '1' else copper_nr_reg when copper_req = '1' else cpu_nr_reg;"),
                ("nr_wr_dat <= copper_nr_dat when copper_req = '1' else cpu_nr_dat;",
                 "nr_wr_dat <= t_nr_dat when t_nr_we = '1' else copper_nr_dat when copper_req = '1' else cpu_nr_dat;"),
                ("port_57_wr <= iowr and port_57;", "port_57_wr <= (iowr and port_57) or t_p57_we;"),
                ("port_5b_wr <= iowr and port_5b;", "port_5b_wr <= (iowr and port_5b) or t_p5b_we;"),
                ("port_303b_wr <= iowr and port_303b;", "port_303b_wr <= (iowr and port_303b) or t_p303b_we;"),
                ("      port5B_w_en_s     => port_5b_wr,\n      port303b_r_en_s   => port_303b_rd,\n      port303b_w_en_s   => port_303b_wr,\n      cpu_d_i           => cpu_do,",
                 "      port5B_w_en_s     => port_5b_wr,\n      port303b_r_en_s   => port_303b_rd,\n      port303b_w_en_s   => port_303b_wr,\n"
                 "      cpu_d_i           => spr_cpu_d,")):
            assert text.count(old) == 1, old
            text = text.replace(old, new, 1)
        old = """         elsif nr_69_we = '1' then
            port_123b_layer2_en <= nr_wr_dat(7);
         end if;"""
        assert old in text
        text = text.replace(old, old + "\n         if t_en(4) = '1' then port_123b_layer2_en <= t_val(32); end if;", 1)
        i = text.index("architecture rtl of zxnext is")
        j = text.index("\nbegin\n", i)
        text = text[:j] + ("\n   signal spr_cpu_d : std_logic_vector(7 downto 0);\nbegin\n"
                           "   spr_cpu_d <= t_p_dat when (t_p57_we or t_p5b_we or t_p303b_we) = '1' else cpu_do;\n"
                           "   p_hc <= std_logic_vector(hc);\n   p_vc <= std_logic_vector(vc);\n"
                           "   p_dbg(0) <= tm_pixel_en; p_dbg(1) <= tm_pixel_en_1; p_dbg(2) <= tm_en_1; p_dbg(15 downto 8) <= tm_pixel;\n") \
               + text[j + len("\nbegin\n"):]
    elif base == "sprites.vhd":
        text = init_signals(text)
        for n in range(5):
            old = f"   attr{n} : sdpram_128_8\n"
            assert old in text
            text = text.replace(old, f"   attr{n} : entity work.sdpram_128_8_{n}\n", 1)
    elif base in ("tdpram.vhd", "sdpram.vhd", "dpram.vhd"):
        pass
    else:
        text = init_signals(text)
    p = os.path.join(out, base)
    open(p, "w").write(text)
    listing.append(p)

shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "tbh.vhd"), os.path.join(out, "tbh.vhd"))
with open(os.path.join(out, "models.vhd"), "w") as fh:
    fh.write(ram_model("sdpram_16_9", 4, 9) + ram_model("sdpram_64_9", 6, 9) + ram_model("sdpram_128_8", 7, 8))
    for n in range(5):
        fh.write(sprite_ram_model(f"sdpram_128_8_{n}", n))
    fh.write(SPRITE_MODELS)
shutil.copytree(os.path.join(src, "ram", "init"), os.path.join(out, "init"), dirs_exist_ok=True)
with open(os.path.join(out, "files.txt"), "w") as fh:
    fh.write(os.path.join(out, "tbh.vhd") + "\n")
    fh.write(os.path.join(out, "models.vhd") + "\n")
    fh.write("\n".join(listing) + "\n")
print(len(listing) + 2, "files in", out)
