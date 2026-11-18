-- POC 051: Pentagon 2.666 LE video fetch oracle.
--
-- The board's zx_main (common/zx_main.vhd, unchanged, with its line buffers common/ram/ram_32.VHD and
-- ram_border.VHD) on mainclk, the SRAM read path of PaE.vhd:1071-1083 and 1135-1141 copied line for line: the video
-- address goes to the SRAM on the main_state = 11 edge, video_data takes the SRAM data on the next main_state = 01
-- edge, zx_main takes video_data on the main_state = 11 edge after that. Upstream revision e808402.
-- otmfnt_rom (an altsyncram) is replaced by a registered ROM of zeros: the text mode is not traced here.
--
-- Trace (edges numbered as in p2666_timing_tb: the number of the mainclk rising edge just taken):
--   INT <n>              int_strobe goes low (the INT strobe, as the timing oracle's)
--   RD <n> <addr>        a new video address (19 bits, bytes) goes to the SRAM on edge n (main_state = 11); the
--                        data is latched into video_data 2 edges later. video_fetch.py sorts them into pixel and
--                        attribute reads by address
--   PAL <n> <pixel_c> <line_c> <index>   the palette index zx_main outputs changed (logged for 20000 edges after
--                        each FE_EDGEn border write)
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity otmfnt_rom is
    port(address : in std_logic_vector(10 downto 0); clock : in std_logic := '1'; q : out std_logic_vector(7 downto 0));
end entity;
architecture sim of otmfnt_rom is
begin
    process(clock) begin if rising_edge(clock) then q <= (others => '0'); end if; end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity p2666_video_tb is
    generic(
        TRACE       : string  := "video.txt";
        MAX_MAINCLK : natural := 4600000;
        GFX_MODE    : std_logic_vector(5 downto 0) := "011000";
        -- border writes: FE[2:0] becomes 2, 5, 3 on these edges (0 = never)
        FE_EDGE1    : natural := 0;
        FE_EDGE2    : natural := 0;
        FE_EDGE3    : natural := 0
    );
end entity;

architecture sim of p2666_video_tb is
    component zx_main is
        port(
            mainclk : in std_logic; ym_clk_ena : out std_logic; main_state_counter : out std_logic_vector(4 downto 0);
            video_data : in std_logic_vector(15 downto 0); portfe : in std_logic_vector(3 downto 0);
            gfx_mode : in std_logic_vector(5 downto 0); int_strobe : out std_logic;
            int_delay : in std_logic_vector(9 downto 0); screen_delay : in std_logic_vector(7 downto 0);
            border_delay : in std_logic_vector(7 downto 0); ssii : out std_logic; ksii : out std_logic;
            vidr : out std_logic_vector(4 downto 0); vidg : out std_logic_vector(4 downto 0);
            vidb : out std_logic_vector(4 downto 0); pixelc : out std_logic_vector(9 downto 0);
            linec : out std_logic_vector(9 downto 0); pollitra_a : out std_logic_vector(3 downto 0);
            pollitra_awr : out std_logic_vector(3 downto 0); pollitra_d : in std_logic_vector(15 downto 0);
            border3 : in std_logic; video_full_addr : out std_logic_vector(20 downto 0);
            port7ffd : in std_logic_vector(7 downto 0));
    end component;

    signal mainclk : std_logic := '0';
    signal edge_n  : natural := 0;
    signal done    : boolean := false;

    signal main_state_counter : std_logic_vector(4 downto 0);
    signal video_data : std_logic_vector(15 downto 0) := (others => '0');
    signal video_full_adr : std_logic_vector(20 downto 0);
    signal radr : std_logic_vector(18 downto 0) := (others => '0');
    signal int_strobe : std_logic;
    signal ym_clk_ena, ssii, ksii : std_logic;
    signal vidr, vidg, vidb : std_logic_vector(4 downto 0);
    signal pixelc, linec : std_logic_vector(9 downto 0);
    signal pollitra_a, pollitra_awr : std_logic_vector(3 downto 0);
    signal portfe : std_logic_vector(3 downto 0) := "0000";

begin
    mainclk <= not mainclk after 1 ns when not done else '0';

    dut : zx_main port map(mainclk, ym_clk_ena, main_state_counter, video_data, portfe, GFX_MODE, int_strobe,
                           "1011000000", x"00", x"00", ssii, ksii, vidr, vidg, vidb, pixelc, linec, pollitra_a,
                           pollitra_awr, x"FFFF", '0', video_full_adr, x"00");

    -- PaE.vhd:1071-1083 (the video half of the SRAM cycle) and 1135-1141; the SRAM answers with the address' low
    -- byte (the trace records addresses, not data).
    process(mainclk)
        file f : text open write_mode is TRACE;
        variable l : line;
        variable int_old : std_logic := '1';
        variable pal_old : std_logic_vector(3 downto 0) := "0000";
        variable n : natural;
    begin
        if rising_edge(mainclk) then
            n := edge_n + 1;  -- the number of this edge
            edge_n <= n;
            if n >= MAX_MAINCLK then done <= true; end if;
            if main_state_counter(1 downto 0) = "11" then
                radr <= video_full_adr(18 downto 0);
                -- a new video address goes to the SRAM on this edge
                if video_full_adr(18 downto 0) /= radr then
                    write(l, string'("RD ")); write(l, n); write(l, string'(" ")); hwrite(l, "0" & video_full_adr(18 downto 0)); writeline(f, l);
                end if;
            end if;
            if main_state_counter(1 downto 0) = "01" then
                video_data <= x"00" & radr(7 downto 0);
            end if;
            if n = FE_EDGE1 then portfe <= "0010"; end if;
            if n = FE_EDGE2 then portfe <= "0101"; end if;
            if n = FE_EDGE3 then portfe <= "0011"; end if;
            -- the palette index on the VGA output after each border write
            if pollitra_a /= pal_old and ((FE_EDGE1 > 0 and n >= FE_EDGE1 and n < FE_EDGE1 + 20000) or
                                          (FE_EDGE2 > 0 and n >= FE_EDGE2 and n < FE_EDGE2 + 20000) or
                                          (FE_EDGE3 > 0 and n >= FE_EDGE3 and n < FE_EDGE3 + 20000)) then
                write(l, string'("PAL ")); write(l, n); write(l, string'(" "));
                write(l, to_integer(unsigned(pixelc))); write(l, string'(" ")); write(l, to_integer(unsigned(linec)));
                write(l, string'(" ")); write(l, to_integer(unsigned(pollitra_a))); writeline(f, l);
            end if;
            pal_old := pollitra_a;
            if int_strobe = '0' and int_old = '1' then
                write(l, string'("INT ")); write(l, n); writeline(f, l);
            end if;
            int_old := int_strobe;
        end if;
    end process;
end architecture;
