-- POC 051: Pentagon 2.666 LE CPU timing oracle.
--
-- The board's T80a (common/T80, unchanged) clocked exactly as PaE.vhd clocks it, with the board's INT. Upstream
-- revision e808402 (https://github.com/koe1234/pentagon_2_666_le). Line references are to that revision.
--
-- Copied line for line (signal names kept):
--   zx_main.vhd:220        main_state counter
--   zx_main.vhd:429-440    pixel_c / line_c raster counters
--   zx_main.vhd:462-468    int_str strobe (int_delay = 704, PaE.vhd:572)
--   PaE.vhd:1543-1566      vsync_int / int_counter (the INT flag of port FE left out)
--   PaE.vhd:1579-1609      cpu_speed and cpu_clk
--   PaE.vhd:1264-1301      the xx77 / EFF7 writes (the clock bits), with wr_ports_single; DOS is on all the time
--                          here (cpm stays 0 after reset), so xx77 is always the ATM port
-- Not modelled: vg_tormoz (the Beta Disk emulator's clock stop) is '0'.
--
-- Memory: 64 KB, read at once, no wait (the board has no memory wait: cpu_wait is the external ZX-BUS WAIT only).
-- Ports read 0xFF.
--
-- Trace (mainclk = rising edges of the 114 MHz clock counted from 0; an event is stamped with the edge that made it):
--   M1 <n> <addr>          opcode fetch: n = the cpu_clk rising edge that starts T1
--   INTA <n> <addr>        interrupt acknowledge cycle (M1 with IORQ)
--   IOW <n> <port> <data>  port write latched (the board's wr_ports_single edge)
--   IOR <n> <port>         port read (IORQ and RD low, first edge)
--   INT <n> <0|1>          vsync_int (the CPU's INT_n) changes
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.std_logic_unsigned.all;
use std.textio.all;

entity p2666_timing_tb is
    generic(
        PROG        : string  := "prog.bin";
        TRACE       : string  := "trace.txt";
        MAX_MAINCLK : natural := 3000000
    );
end entity;

architecture sim of p2666_timing_tb is
    component T80a
        generic(Mode : integer := 0);
        port(
            RESET_n : in std_logic; CLK_n : in std_logic; WAIT_n : in std_logic; INT_n : in std_logic;
            NMI_n : in std_logic; BUSRQ_n : in std_logic; M1_n : out std_logic; MREQ_n : out std_logic;
            IORQ_n : out std_logic; RD_n : out std_logic; WR_n : out std_logic; RFSH_n : out std_logic;
            HALT_n : out std_logic; BUSAK_n : out std_logic; A : out std_logic_vector(15 downto 0);
            DOUT : out std_logic_vector(7 downto 0); DIN : in std_logic_vector(7 downto 0);
            IOcycle : out std_logic; MEMcycle : out std_logic);
    end component;

    constant horizontal_pixels : natural := 896;
    constant vertical_pixels   : natural := 640;

    type mem_t is array (0 to 65535) of std_logic_vector(7 downto 0);

    impure function load_mem(path : string) return mem_t is
        type bin_file is file of character;
        file f : bin_file open read_mode is path;
        variable m : mem_t := (others => x"00");
        variable c : character;
        variable i : natural := 0;
    begin
        while not endfile(f) and i < 65536 loop
            read(f, c);
            m(i) := std_logic_vector(to_unsigned(character'pos(c), 8));
            i := i + 1;
        end loop;
        return m;
    end function;

    signal mem : mem_t := load_mem(PROG);

    signal mainclk : std_logic := '0';
    signal edge_n  : natural := 0;      -- number of the mainclk rising edge just taken
    signal done    : boolean := false;

    signal main_state : std_logic_vector(4 downto 0) := "00000";
    signal main_state_counter : std_logic_vector(4 downto 0);
    signal pixel_c, line_c : std_logic_vector(9 downto 0) := (others => '0');
    signal int_delay : std_logic_vector(9 downto 0) := b"1011000000";
    signal int_str, int_strobe : std_logic := '1';

    signal cpu_res : std_logic := '0';
    signal cpu_clk, cpu_clk_b, cpu_clk_old : std_logic := '0';
    signal cpu_speed : std_logic_vector(2 downto 0);
    signal vg_tormoz : std_logic := '0';
    signal vsync_int : std_logic := '1';
    signal int_counter : std_logic_vector(4 downto 0) := (others => '0');
    signal uart_int : std_logic := '1';
    signal internal_int : std_logic;
    signal cpu_int : std_logic;

    signal cpu_m1, cpu_mreq, cpu_iorq, cpu_rd, cpu_wr, cpu_rfsh, cpu_halt, cpu_busack : std_logic;
    signal cpu_a : std_logic_vector(15 downto 0);
    signal cpu_dout, cpu_din : std_logic_vector(7 downto 0);
    signal iocycle, memcycle : std_logic;

    signal portxx77 : std_logic_vector(7 downto 0) := b"10101011";
    signal porteff7 : std_logic_vector(7 downto 0) := (others => '0');
    signal wr_ports_single : std_logic := '0';
    signal cpu_iowr, cpu_iord : std_logic;
    signal ebl : std_logic;

    function hex(v : std_logic_vector) return string is
        constant digits : string(1 to 16) := "0123456789ABCDEF";
        variable r : string(1 to v'length / 4);
        variable nib : natural;
    begin
        for i in 0 to v'length / 4 - 1 loop
            nib := to_integer(unsigned(v(v'low + 4 * (v'length / 4 - 1 - i) + 3 downto v'low + 4 * (v'length / 4 - 1 - i))));
            r(i + 1) := digits(nib + 1);
        end loop;
        return r;
    end function;
begin
    -- 114 MHz; the period does not matter, only edge counts are traced
    mainclk <= not mainclk after 1 ns when not done else '0';

    process(mainclk)
    begin
        if rising_edge(mainclk) then
            edge_n <= edge_n + 1;
            if edge_n = 64 then cpu_res <= '1'; end if;   -- the MCU releases the CPU after the upload
            if edge_n >= MAX_MAINCLK then done <= true; end if;
        end if;
    end process;

    -- zx_main.vhd:220 / :429-440 / :462-468 (only what drives int_str)
    process(mainclk)
    begin
        if (mainclk'event and mainclk = '1') then
            main_state(4 downto 0) <= main_state(4 downto 0) + '1';
            if (main_state(1 downto 0) = b"11") then
                pixel_c <= pixel_c + '1';
                if (pixel_c > (horizontal_pixels-2)) then
                    pixel_c <= (others => '0'); line_c <= line_c+'1';
                    if (line_c > (vertical_pixels-2)) then line_c <= (others => '0'); end if;
                end if;
                if(line_c = (vertical_pixels-1-32) and (( (pixel_c >(int_delay(9 downto 0))) and (pixel_c < (int_delay(9 downto 0)+2)) ) )) then
                    int_str<='0';
                else int_str<='1';
                end if;
            end if;
        end if;
    end process;
    main_state_counter(4 downto 0) <= main_state(4 downto 0);
    int_strobe <= int_str;

    -- PaE.vhd:1543-1566 (vsync int; the FE flag is not traced)
    process(mainclk)
    begin
        if(mainclk'event and mainclk = '1') then
            cpu_clk_old <= cpu_clk;
            if(cpu_res = '0') then vsync_int <= '1'; int_counter(4 downto 0) <= (others => '0'); end if;
            if(int_strobe = '0') then
                vsync_int <= '0';
                int_counter(4 downto 0) <= (others => '1');
            end if;
            if(cpu_clk_old = '0' and cpu_clk = '1') then
                if(int_counter(4 downto 0) /= b"00000") then
                    int_counter(4 downto 0) <= int_counter(4 downto 0) - '1';
                else
                    vsync_int <= '1';
                end if;
            end if;
        end if;
    end process;
    internal_int <= vsync_int and uart_int;
    -- PaE.vhd:1567, 1652: the CPU's INT is fpga_int_input, the internal INT looped out (fpga_int_output) and back
    -- through the ZX-BUS pins. The loop's delay (output buffer, trace, input buffer) is shorter than one mainclk period:
    -- a CPU clock edge on the same mainclk edge as the INT change still sees the old level. Half a period here
    cpu_int <= internal_int after 500 ps;

    -- PaE.vhd:1579-1609
    cpu_speed(2 downto 0) <= not(iocycle) & portxx77(3) & porteff7(4);

    process(mainclk)
    begin
        if(mainclk'event and mainclk = '1') then
            case cpu_speed(2 downto 1) is -- 28 MHz
                when b"11" =>
                    case main_state_counter(1 downto 0) is
                        when b"01" => cpu_clk_b <= '1';  cpu_clk <= '1';
                        when b"11" => cpu_clk_b <= '0'; if(vg_tormoz = '0') then cpu_clk <= '0'; end if;
                        when others => null;
                    end case;
                when others =>
                    if(cpu_speed(2 downto 0) = b"100") then -- 14 MHz
                        case main_state_counter(2 downto 0) is
                            when b"011" => cpu_clk_b <= '0'; if(vg_tormoz = '0') then cpu_clk <= '0'; end if;
                            when b"111" => cpu_clk_b <= '1'; cpu_clk <= '1';
                            when others => null;
                        end case;
                    else -- 3.5 MHz
                        case main_state_counter(4 downto 0) is
                            when b"01111" => cpu_clk_b <= '0'; if(vg_tormoz = '0') then cpu_clk <= '0'; end if;
                            when b"11111" => cpu_clk_b <= '1'; cpu_clk <= '1';
                            when others => null;
                        end case;
                    end if;
            end case;
        end if;
    end process;

    cpu : T80a port map(cpu_res, cpu_clk, '1', cpu_int, '1', '1', cpu_m1, cpu_mreq, cpu_iorq, cpu_rd, cpu_wr,
                        cpu_rfsh, cpu_halt, cpu_busack, cpu_a, cpu_dout, cpu_din, iocycle, memcycle);

    cpu_iowr <= cpu_iorq or cpu_wr;
    cpu_iord <= cpu_iorq or cpu_rd;
    ebl <= (not(cpu_m1) or cpu_a(1) or cpu_a(2));

    -- PaE.vhd:1493 (INT acknowledge reads 0xFF), memory, ports 0xFF
    cpu_din <= x"FF" when (cpu_iorq = '0' and cpu_m1 = '0')
               else mem(to_integer(unsigned(cpu_a))) when cpu_mreq = '0'
               else x"FF";

    -- memory write; PaE.vhd:1228-1305 for the clock bits
    process(mainclk, cpu_res, cpu_iorq)
    begin
        if(cpu_res = '0') then
            porteff7(7 downto 0) <= (others => '0');
            portxx77(7 downto 0) <= b"10101011";
            wr_ports_single <= '0';
        elsif(cpu_iorq = '1') then
            wr_ports_single <= '0';
        elsif(mainclk'event and mainclk = '1') then
            if(cpu_iowr='0' and ebl='1' and wr_ports_single = '0') then
                wr_ports_single <= '1';
                if(cpu_a(3)='0' and cpu_a(5)='1' and cpu_a(12)='0' and cpu_a(15 downto 13) = b"111" and cpu_a(0)='1') then
                    porteff7(7 downto 0) <= cpu_dout(7 downto 0);
                end if;
                if(cpu_a(7 downto 0) = x"77") then
                    portxx77(7 downto 0) <= cpu_dout(7 downto 0);
                end if;
            end if;
        end if;
    end process;

    process(mainclk)
    begin
        if(mainclk'event and mainclk = '1') then
            if cpu_mreq = '0' and cpu_wr = '0' then
                mem(to_integer(unsigned(cpu_a))) <= cpu_dout;
            end if;
        end if;
    end process;

    -- trace
    process(mainclk)
        file tf : text open write_mode is TRACE;
        variable l : line;
        variable last_rise : natural := 0;
        variable prev_m1, prev_iorq, prev_int : std_logic := '1';
        variable prev_iowr_single : std_logic := '0';
        variable io_rd_logged : boolean := false;
    begin
        if rising_edge(mainclk) then
            -- this edge is number edge_n + 1 (edge_n updates with it)
            if cpu_clk_old = '0' and cpu_clk = '1' then
                last_rise := edge_n;   -- cpu_clk rose on the previous edge, numbered edge_n
            end if;
            if cpu_m1 = '0' and prev_m1 = '1' then
                write(l, string'("M1 ")); write(l, last_rise); write(l, string'(" ")); write(l, hex(cpu_a));
                writeline(tf, l);
            end if;
            if cpu_m1 = '0' and cpu_iorq = '0' and prev_iorq = '1' then
                write(l, string'("INTA ")); write(l, edge_n); write(l, string'(" ")); write(l, hex(cpu_a));
                writeline(tf, l);
            end if;
            if wr_ports_single = '1' and prev_iowr_single = '0' then
                write(l, string'("IOW ")); write(l, edge_n); write(l, string'(" ")); write(l, hex(cpu_a));
                write(l, string'(" ")); write(l, hex(cpu_dout));
                writeline(tf, l);
            end if;
            if cpu_m1 = '1' and cpu_iorq = '0' and cpu_rd = '0' and not io_rd_logged then
                write(l, string'("IOR ")); write(l, edge_n); write(l, string'(" ")); write(l, hex(cpu_a));
                writeline(tf, l);
                io_rd_logged := true;
            end if;
            if cpu_iorq = '1' then io_rd_logged := false; end if;
            if internal_int /= prev_int then
                write(l, string'("INT ")); write(l, edge_n); write(l, string'(" "));
                if internal_int = '1' then write(l, string'("1")); else write(l, string'("0")); end if;
                writeline(tf, l);
            end if;
            prev_m1 := cpu_m1;
            prev_iorq := cpu_iorq;
            prev_int := internal_int;
            prev_iowr_single := wr_ports_single;
        end if;
    end process;
end architecture;
