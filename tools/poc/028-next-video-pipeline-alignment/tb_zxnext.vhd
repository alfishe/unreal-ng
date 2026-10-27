-- POC 028 testbench: the whole zxnext core with the clocks and the SRAM arbiter of the top level, one register forced at a chosen clock
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use work.tbh.all;

entity tb_zxnext is
   generic (
      g_scenario : natural := 0;   -- 0 = nothing forced, otherwise the register under test (see the case below)
      g_ref      : natural := 0;   -- 1 = the reference run: the same sequence, the value never changed (analyze.py diff)
      g_l2const  : natural := 0;   -- Layer 2 SRAM returns this byte everywhere (0 = the x pattern)
      g_ahead    : natural := 2;   -- sprite scenarios: the row / sprite chosen is the one shown this many lines after the force
      g_h0       : natural := 200; -- the pixel counter value the sweep's clock offsets start from
      g_log      : string  := "dump.txt";
      g_events   : string  := "events.txt"
   );
end entity;

architecture sim of tb_zxnext is
   constant T28 : time := 36 ns;

   signal clk28, clk28_n, clk14, clk7, clk_cpu, psg_en : std_logic := '0';
   signal clk3m5_cont : std_logic := '1';
   signal cpu_speed : std_logic_vector(1 downto 0);
   signal cpu_contend, cpu_clk_lsb : std_logic;
   signal reset : std_logic := '1';

   signal rgb : std_logic_vector(8 downto 0);
   signal rgb_vs_n, rgb_hs_n, rgb_bk_n : std_logic;

   signal ram_a_addr_21 : std_logic;
   signal ram_a_addr : std_logic_vector(20 downto 0);
   signal ram_a_req, ram_a_rd_n : std_logic;
   signal ram_a_di, ram_a_do : std_logic_vector(7 downto 0);
   signal ram_b_addr : std_logic_vector(20 downto 0);
   signal ram_b_req_t : std_logic;
   signal ram_b_di : std_logic_vector(7 downto 0);

   -- the top level's sram arbiter (zxnext_top_issue5.vhd 1177-1250) against a model SRAM
   signal sram_port_b_req, zxn_ram_b_req : std_logic := '0';
   signal sram_addr : std_logic_vector(21 downto 0);
   signal sram_rd_n : std_logic;
   signal sram_oe_n_active : std_logic := '0';
   signal sram_port_a_active, sram_port_b_active, sram_data_h_active : std_logic := '0';
   signal sram_port_a_dat, sram_port_b_dat : std_logic_vector(7 downto 0) := (others => '0');
   signal ram_addr_o : std_logic_vector(20 downto 0) := (others => '0');
   signal ram_data : std_logic_vector(15 downto 0);
   signal ram_cs_n : std_logic := '1';

   signal zeros12 : std_logic_vector(11 downto 0) := (others => '0');
   signal zeros28 : std_logic_vector(27 downto 0) := (others => '0');
   signal kbd_col : std_logic_vector(4 downto 0) := (others => '1');

   signal hc_s, vc_s : unsigned(8 downto 0);
   signal count28 : natural := 0;
   ----------------------------------------------------------------- the registers under test (VHDL-2008 external names)
begin
   ----------------------------------------------------------------- the core
   dut : entity work.zxnext
      generic map (
         g_machine_id  => X"0A",
         g_video_def   => "000",
         g_version     => X"32",
         g_sub_version => X"03",
         g_board_issue => X"3",
         g_video_inc   => "11"
      )
      port map (
         i_CLK_28 => clk28, i_CLK_28_n => clk28_n, i_CLK_14 => clk14, i_CLK_7 => clk7, i_CLK_CPU => clk_cpu, i_CLK_PSG_EN => psg_en,
         o_CPU_SPEED => cpu_speed, o_CPU_CONTEND => cpu_contend, o_CPU_CLK_LSB => cpu_clk_lsb,
         i_RESET => reset,
         o_RESET_SOFT => open, o_RESET_HARD => open, o_RESET_PERIPHERAL => open,
         o_FLASH_BOOT => open, o_CORE_ID => open,
         i_SPKEY_FUNCTION => "0000000000", i_SPKEY_BUTTONS => "00",
         o_KBD_CANCEL => open, o_KBD_ROW => open, i_KBD_COL => kbd_col, i_KBD_EXTENDED_KEYS => x"0000",
         o_KEYMAP_ADDR => open, o_KEYMAP_DATA => open, o_KEYMAP_WE => open, o_JOYMAP_WE => open,
         i_JOY_LEFT => zeros12, i_JOY_RIGHT => zeros12,
         o_JOY_IO_MODE_EN => open, o_JOY_IO_MODE_PIN_7 => open, o_JOY_LEFT_TYPE => open, o_JOY_RIGHT_TYPE => open,
         i_MOUSE_X => x"00", i_MOUSE_Y => x"00", i_MOUSE_BUTTON => "000", i_MOUSE_WHEEL => "0000",
         o_PS2_MODE => open, o_MOUSE_CONTROL => open,
         i_I2C_SCL_n => '1', i_I2C_SDA_n => '1', o_I2C_SCL_n => open, o_I2C_SDA_n => open,
         o_SPI_SS_FLASH_n => open, o_SPI_SS_SD1_n => open, o_SPI_SS_SD0_n => open, o_SPI_SCK => open, o_SPI_MOSI => open,
         i_SPI_SD_MISO => '1', i_SPI_FLASH_MISO => '1',
         i_UART0_RX => '1', o_UART0_TX => open, i_UART0_CTS_n => '0', o_UART0_RTR_n => open,
         o_RGB => rgb, o_RGB_VS_n => rgb_vs_n, o_RGB_HS_n => rgb_hs_n, o_RGB_BK_n => rgb_bk_n,
         o_VIDEO_50_60 => open, o_VIDEO_SCANLINES => open, o_VIDEO_SCANDOUBLE => open, o_VIDEO_MODE => open, o_MACHINE_TIMING => open,
         o_HDMI_RESET => open, o_HDMI_PIXEL => open, o_HDMI_LOCK => open,
         o_AUDIO_HDMI_AUDIO_EN => open, o_AUDIO_SPEAKER_EN => open, o_AUDIO_SPEAKER_EXCL => open,
         i_AUDIO_EAR => '0', o_AUDIO_PI_FE_EAR => open, o_AUDIO_ISSUE2_FE_MIC => open, o_AUDIO_MIC => open, o_AUDIO_EAR => open,
         o_AUDIO_L => open, o_AUDIO_R => open,
         o_RAM_A_ADDR_21 => ram_a_addr_21, o_RAM_A_ADDR => ram_a_addr, o_RAM_A_REQ => ram_a_req, o_RAM_A_RD_n => ram_a_rd_n,
         i_RAM_A_DI => ram_a_di, o_RAM_A_DO => ram_a_do,
         o_RAM_B_ADDR => ram_b_addr, o_RAM_B_REQ_T => ram_b_req_t, i_RAM_B_DI => ram_b_di,
         o_BUS_ADDR => open, i_BUS_DI => x"FF", o_BUS_DO => open, o_BUS_MREQ_n => open, o_BUS_IORQ_n => open, o_BUS_RD_n => open,
         o_BUS_WR_n => open, o_BUS_M1_n => open, i_BUS_WAIT_n => '1', i_BUS_NMI_n => '1', i_BUS_INT_n => '1', o_BUS_INT_n => open,
         i_BUS_BUSREQ_n => '1', o_BUS_BUSAK_n => open, o_BUS_HALT_n => open, o_BUS_RFSH_n => open, o_BUS_IEO => open,
         i_BUS_ROMCS_n => '1', i_BUS_IORQULA_n => '1',
         o_BUS_P3_MTR_n => open, o_BUS_P3_DRD_n => open, o_BUS_P3_DWR_n => open,
         o_BUS_EN => open, o_BUS_CLKEN => open, o_BUS_NMI_DEBOUNCE_DISABLE => open,
         i_ESP_GPIO_20 => "111", o_ESP_GPIO_0 => open, o_ESP_GPIO_0_EN => open,
         i_GPIO => zeros28, o_GPIO => open, o_GPIO_EN => open,
         o_XDNA_LOAD => open, o_XDNA_SHIFT => open, i_XDNA_DO => '0',
         o_XADC_RESET => open, o_XADC_DEN => open, o_XADC_DADDR => open, o_XADC_DWE => open, i_XADC_DRDY => '0',
         o_XADC_DI => open, i_XADC_DO => x"0000", i_XADC_BUSY => '0', i_XADC_EOC => '0', i_XADC_EOS => '0',
         o_XADC_CONVST => open, o_XADC_CONTROL => open
      );

   ----------------------------------------------------------------- clocks
   process
   begin
      loop
         -- cycle 0
         clk28 <= '1'; clk14 <= '1'; clk7 <= '1'; wait for T28 / 2; clk28 <= '0'; wait for T28 / 2;
         -- cycle 1
         clk28 <= '1'; clk14 <= '0'; clk7 <= '1'; wait for T28 / 2; clk28 <= '0'; wait for T28 / 2;
         -- cycle 2
         clk28 <= '1'; clk14 <= '1'; clk7 <= '0'; wait for T28 / 2; clk28 <= '0'; wait for T28 / 2;
         -- cycle 3
         clk28 <= '1'; clk14 <= '0'; clk7 <= '0'; wait for T28 / 2; clk28 <= '0'; wait for T28 / 2;
      end loop;
   end process;
   clk28_n <= not clk28;

   process (clk28)
      variable n : natural := 0;
   begin
      if rising_edge(clk28) then
         n := (n + 1) mod 16;
         if n = 15 then psg_en <= '1'; else psg_en <= '0'; end if;
         count28 <= count28 + 1;
      end if;
   end process;

   -- cpu clock (3.5 MHz only): zxnext_top_issue5.vhd 1017-1052
   process (clk7)
   begin
      if rising_edge(clk7) then
         if cpu_clk_lsb = '1' and cpu_contend = '0' then
            clk3m5_cont <= '0';
         elsif cpu_clk_lsb = '0' then
            clk3m5_cont <= '1';
         end if;
      end if;
   end process;
   clk_cpu <= clk3m5_cont;

   process
   begin
      reset <= '1';
      wait for 40 * T28;
      reset <= '0';
      wait;
   end process;

   ----------------------------------------------------------------- the sram
   zxn_ram_b_req <= (ram_b_req_t xor sram_port_b_req) and not ram_a_req;
   sram_addr <= (ram_a_addr_21 & ram_a_addr) when ram_a_req = '1' else ('0' & ram_b_addr);
   sram_rd_n <= ram_a_rd_n and ram_a_req;

   process (clk28)
   begin
      if rising_edge(clk28) then
         if zxn_ram_b_req = '1' then
            sram_port_b_req <= ram_b_req_t;
         end if;
         ram_cs_n <= not (ram_a_req or zxn_ram_b_req);
         ram_addr_o <= sram_addr(21 downto 1);
         sram_oe_n_active <= sram_rd_n;
         sram_port_a_active <= ram_a_req;
         sram_port_b_active <= zxn_ram_b_req;
         sram_data_h_active <= sram_addr(0);
         -- read data: the model SRAM answers in the same cycle (word = address-derived)
         if sram_oe_n_active = '0' then
            if sram_port_a_active = '1' then
               sram_port_a_dat <= ram_data(7 downto 0) when sram_data_h_active = '0' else ram_data(15 downto 8);
            end if;
            if sram_port_b_active = '1' then
               sram_port_b_dat <= ram_data(7 downto 0) when sram_data_h_active = '0' else ram_data(15 downto 8);
            end if;
         end if;
      end if;
   end process;
   ram_a_di <= sram_port_a_dat;
   ram_b_di <= sram_port_b_dat;
   -- byte = low 8 bits of the byte address, bit 7 flipped on odd rows (address bit 8): Layer 2's pixel (x, y) in 256 x 192 is x xor (y odd ? 128 : 0)
   process (ram_addr_o)
      variable w : unsigned(6 downto 0);
      variable lo, hi : unsigned(7 downto 0);
   begin
      w := unsigned(ram_addr_o(6 downto 0));
      lo := w & '0';
      hi := w & '1';
      if ram_addr_o(7) = '1' then
         lo(7) := not lo(7);
         hi(7) := not hi(7);
      end if;
      if g_l2const /= 0 then
         lo := to_unsigned(g_l2const, 8);
         hi := to_unsigned(g_l2const, 8);
      end if;
      ram_data <= std_logic_vector(hi) & std_logic_vector(lo);
   end process;

   ----------------------------------------------------------------- stimulus
   process
      file ev : text open write_mode is g_events;
      variable l : line;
      constant first_line : natural := 70;
      variable k : natural := 0;
      variable off : natural;
      variable line_no : natural;
      variable r_idx : natural := 0;
      variable r_base, r_new : std_logic_vector(7 downto 0) := x"00";
      variable pal : boolean := false;
      variable kind : natural := 0;          -- 2 = an injected NextREG write, 3 = a sprite pattern byte through port #5B
      variable n_reg : std_logic_vector(7 downto 0) := x"00";
      variable n_base, n_new : std_logic_vector(7 downto 0) := x"00";
      variable n_sel : boolean := false;     -- select the sprite that covers the line (NR #34) before the write
      variable pat_n : natural := 0;
      variable spr : natural := 0;
      procedure nr_write(r, v : std_logic_vector(7 downto 0)) is
      begin
         t_nr_reg <= r; t_nr_dat <= v; t_nr_we <= '1';
         wait for T28;
         t_nr_we <= '0';
         wait for T28;
      end procedure;
      procedure p57_write(v : std_logic_vector(7 downto 0)) is
      begin
         t_p_dat <= v; t_p57_we <= '1';
         wait for T28;
         t_p57_we <= '0';
         wait for T28;
      end procedure;
      procedure p303b_write(v : natural) is
      begin
         t_p_dat <= std_logic_vector(to_unsigned(v, 8)); t_p303b_we <= '1';
         wait for T28;
         t_p303b_we <= '0';
         wait for T28;
      end procedure;
      procedure p5b_write(v : std_logic_vector(7 downto 0)) is
      begin
         t_p_dat <= v; t_p5b_we <= '1';
         wait for T28;
         t_p5b_we <= '0';
         wait for T28;
      end procedure;
      variable pal_idx : natural := 0;
      variable pal_base, pal_new : natural := 0;
      procedure pal_write(idx : natural; v : natural) is
      begin
         t_pal_idx <= std_logic_vector(to_unsigned(idx, 8));
         t_pal_val <= std_logic_vector(to_unsigned(v, 9));
         t_pal_we <= '1';
         wait for T28;
         t_pal_we <= '0';
      end procedure;
      procedure set_reg(idx : natural; v : std_logic_vector(7 downto 0)) is
      begin
         t_en(idx) <= '1';
         t_val(idx * 8 + 7 downto idx * 8) <= v;
      end procedure;
   begin
      wait until reset = '0';
      wait for 200 * T28;
      t_hold <= '1';     -- the CPU runs the boot ROM: hold it so that it cannot write the registers under test
      -- baseline of every scenario: ULA on with its pattern, everything else off, fallback bright yellow
      set_reg(0, x"01"); set_reg(1, x"00"); set_reg(2, x"00"); set_reg(3, x"00"); set_reg(4, x"00");
      set_reg(5, x"00"); set_reg(6, x"00"); set_reg(7, x"00");
      set_reg(8, x"00"); set_reg(9, x"FF"); set_reg(10, x"00"); set_reg(11, x"FF");
      set_reg(12, x"FC"); set_reg(13, x"00"); set_reg(14, x"E3");
      set_reg(15, x"00"); set_reg(16, x"00"); set_reg(17, x"BF"); set_reg(18, x"00"); set_reg(19, x"00");
      set_reg(20, x"00"); set_reg(21, x"BF");
      set_reg(22, x"00"); set_reg(23, x"00"); set_reg(24, x"00"); set_reg(25, x"00"); set_reg(26, x"9F"); set_reg(27, x"00"); set_reg(28, x"FF");
      set_reg(29, x"00"); set_reg(30, x"00"); set_reg(31, x"00"); set_reg(32, x"00"); set_reg(33, x"00"); set_reg(34, x"00");
      set_reg(35, x"00"); set_reg(36, x"07"); set_reg(37, x"0F");
      case g_scenario is
         when 1  => r_idx := 0;  r_base := x"01"; r_new := x"00";                          -- ULA enable
         when 2  => r_idx := 12; r_base := x"FC"; r_new := x"1C"; set_reg(0, x"00");       -- fallback colour
         when 3  => r_idx := 5;  r_base := x"00"; r_new := x"08";                          -- ULA scroll X (8 px)
         when 4  => r_idx := 8;  r_base := x"00"; r_new := x"80";                          -- ULA clip x1
         when 5  => r_idx := 9;  r_base := x"FF"; r_new := x"80";                          -- ULA clip x2
         when 6  => r_idx := 4;  r_base := x"00"; r_new := x"01";                          -- Layer 2 enable
         when 7  => r_idx := 7;  r_base := x"00"; r_new := x"10"; set_reg(4, x"01");       -- Layer 2 scroll X
         when 8  => r_idx := 10; r_base := x"00"; r_new := x"80"; set_reg(4, x"01");       -- Layer 2 clip x1
         when 9  => r_idx := 6;  r_base := x"00"; r_new := x"01";                          -- ULA scroll Y
         when 10 => r_idx := 29; r_base := x"00"; r_new := x"01";                          -- ULA fine scroll X
         when 11 => r_idx := 11; r_base := x"FF"; r_new := x"80"; set_reg(4, x"01");       -- Layer 2 clip x2
         when 12 => r_idx := 15; r_base := x"00"; r_new := x"01"; set_reg(4, x"01");       -- Layer 2 scroll Y
         when 13 => r_idx := 19; r_base := x"00"; r_new := x"01"; set_reg(4, x"01");       -- Layer 2 palette offset
         when 14 => r_idx := 3;  r_base := x"00"; r_new := x"01";                          -- tilemap enable
         when 15 => r_idx := 22; r_base := x"00"; r_new := x"01"; set_reg(3, x"01");       -- tilemap scroll X
         when 16 => r_idx := 25; r_base := x"00"; r_new := x"40"; set_reg(3, x"01");       -- tilemap clip x1
         when 17 => r_idx := 24; r_base := x"00"; r_new := x"01"; set_reg(3, x"01");       -- tilemap scroll Y
         when 18 => r_idx := 13; r_base := x"00"; r_new := x"05"; set_reg(4, x"01");       -- layer order SLU -> ULS
         when 19 => r_idx := 14; r_base := x"E3"; r_new := x"00";                          -- global transparency
         when 20 => r_idx := 30; r_base := x"00"; r_new := x"01";                          -- ULA palette select
         when 21 => r_idx := 31; r_base := x"00"; r_new := x"01"; set_reg(4, x"01");       -- Layer 2 palette select
         when 22 => r_idx := 32; r_base := x"00"; r_new := x"10"; set_reg(3, x"01");       -- tilemap palette select
         when 23 => r_idx := 21; r_base := x"BF"; r_new := x"00";                          -- ULA clip y2
         when 24 => r_idx := 35; r_base := x"00"; r_new := x"01"; set_reg(36, x"0F");       -- ULANext enable (format #0F)
         when 25 => r_idx := 36; r_base := x"07"; r_new := x"0F"; set_reg(35, x"01");       -- ULANext format
         when 26 => r_idx := 37; r_base := x"0F"; r_new := x"00"; set_reg(3, x"01");        -- tilemap transparent index
         when 27 => r_idx := 20; r_base := x"00"; r_new := x"FF";                          -- ULA clip y1 (rows below are visible only)
         -- the second set of POC 028 (the differential runs, analyze.py diff)
         when 28 => r_idx := 33; r_base := x"00"; r_new := x"02"; set_reg(2, x"01");       -- LoRes scroll X (1 LoRes pixel)
         when 29 => r_idx := 34; r_base := x"00"; r_new := x"02"; set_reg(2, x"01");       -- LoRes scroll Y
         when 30 => r_idx := 1;  r_base := x"00"; r_new := x"01";                          -- sprite enable
         when 31 => r_idx := 38; r_base := x"00"; r_new := x"80"; set_reg(1, x"01");       -- sprite clip x1
         when 32 => r_idx := 39; r_base := x"FF"; r_new := x"80"; set_reg(1, x"01");       -- sprite clip x2
         when 33 => r_idx := 40; r_base := x"00"; r_new := x"FF"; set_reg(1, x"01");       -- sprite clip y1
         when 34 => r_idx := 41; r_base := x"BF"; r_new := x"00"; set_reg(1, x"01");       -- sprite clip y2
         when 35 => pal := true; pal_idx := 2;   pal_base := 2;   pal_new := 511;          -- ULA palette value
         when 36 => pal := true; pal_idx := 5;   pal_base := 5;   pal_new := 511; set_reg(4, x"01"); set_reg(42, x"01");  -- Layer 2 palette value (the SRAM returns 5)
         when 37 => pal := true; pal_idx := 161; pal_base := 161; pal_new := 511; set_reg(1, x"01"); set_reg(42, x"02");  -- sprite palette value (#21 + offset 8)
         when 38 => pal := true; pal_idx := 0;   pal_base := 0;   pal_new := 511; set_reg(3, x"01"); set_reg(42, x"03");  -- tilemap palette value
         when 39 => r_idx := 37; r_base := x"0F"; r_new := x"00"; set_reg(3, x"01");       -- tilemap transparent index (again, diff)
         when 40 => r_idx := 32; r_base := x"00"; r_new := x"01"; set_reg(3, x"01");       -- tilemap: force over the ULA (NR #6B bit 0)
         when 41 => r_idx := 32; r_base := x"00"; r_new := x"02"; set_reg(3, x"01");       -- tilemap: 512 tiles (NR #6B bit 1)
         when 42 => r_idx := 32; r_base := x"00"; r_new := x"40"; set_reg(3, x"01");       -- tilemap: 80 columns (NR #6B bit 6)
         when 43 => r_idx := 32; r_base := x"00"; r_new := x"20"; set_reg(3, x"01");       -- tilemap: no attribute byte (NR #6B bit 5)
         -- the third set: injected NextREG writes and sprite RAM writes (the sprite grid: sprite 16 r + c at display row 16 r, column 16 c,
         -- rows 0-3 visible from line 64)
         when 44 => kind := 4; n_base := x"70"; n_new := x"30"; set_reg(1, x"01");                                 -- sprite attribute 0 (X), port #57
         when 45 => kind := 4; n_base := x"80"; n_new := x"00"; set_reg(1, x"01");                                 -- sprite attribute 3 (visible), port #57
         when 46 => kind := 3; n_base := x"21"; n_new := x"5A"; set_reg(1, x"01");                                 -- pattern byte, port #5B
         when 47 => kind := 2; n_reg := x"4B"; n_base := x"E3"; n_new := x"21"; set_reg(1, x"01");                 -- sprite transparent index (pattern byte)
         when 48 => kind := 2; n_reg := x"4B"; n_base := x"E3"; n_new := x"A1"; set_reg(1, x"01");                 -- ... the byte with the palette offset
         when 49 => kind := 2; n_reg := x"6A"; n_base := x"00"; n_new := x"20"; set_reg(2, x"01");                 -- LoRes: Radastan mode
         when 50 => kind := 2; n_reg := x"6A"; n_base := x"20"; n_new := x"30"; set_reg(2, x"01");                 -- Radastan: the XOR bit
         when 51 => kind := 2; n_reg := x"6A"; n_base := x"00"; n_new := x"05"; set_reg(2, x"01");                 -- LoRes palette offset
         when 52 => kind := 2; n_reg := x"70"; n_base := x"00"; n_new := x"10"; set_reg(4, x"01");                 -- Layer 2 resolution 320x256
         when 53 => r_idx := 32; r_base := x"00"; r_new := x"08"; set_reg(3, x"01"); set_reg(0, x"00");  -- tilemap text mode (NR #6B bit 3)
         when others => null;
      end case;
      if g_scenario = 50 then nr_write(x"6A", x"20"); end if;
      if g_ref = 1 then r_new := r_base; pal_new := pal_base; n_new := n_base; end if;
      if g_scenario = 0 then wait; end if;
      wait until unsigned(p_vc) = to_unsigned(10, 9);
      wait until unsigned(p_vc) = to_unsigned(11, 9);   -- one frame to settle
      k := 0;
      while k < 32 loop
         line_no := first_line + 4 * k;
         off := k;
         if kind /= 0 then
            -- the preparation in the line before: select the sprite that covers the line / walk the pattern pointer to the row
            wait until unsigned(p_vc) = to_unsigned(line_no - 1, 9) and unsigned(p_hc) = to_unsigned(100, 9);
            if kind = 2 and n_sel then
               spr := 64 + ((line_no + g_ahead - 64) / 16) * 16 + 5;   -- sprites 64-127 are the rows the lines 64-127 show (the engine's vertical counter is 33 ahead)
               if spr > 127 then spr := 127; end if;
               nr_write(x"34", std_logic_vector(to_unsigned(spr, 8)));
            elsif kind = 3 then
               pat_n := ((line_no + g_ahead - 64) mod 16) * 16;   -- the sprite fill is a line ahead: the row shown two lines later (grid origin: line 64)
               p303b_write(0);
               for i in 1 to pat_n loop p5b_write(n_base); end loop;
            elsif kind = 4 then
               spr := 64 + ((line_no + g_ahead - 64) / 16) * 16 + 5;   -- sprites 64-127 are the rows the lines 64-127 show (the engine's vertical counter is 33 ahead)
               if spr > 127 then spr := 127; end if;
               p303b_write(spr);
               if g_scenario = 45 then
                  p57_write(std_logic_vector(to_unsigned(32 + (spr mod 16) * 16, 8)));
                  p57_write(std_logic_vector(to_unsigned(32 + (spr / 16) * 16, 8)));
                  p57_write(x"80");
               end if;
            end if;
         end if;
         wait until unsigned(p_vc) = to_unsigned(line_no, 9) and unsigned(p_hc) = to_unsigned(g_h0, 9);
         wait for off * T28;
         write(l, string'("force "));
         write(l, k);
         write(l, string'(" "));
         write(l, line_no);
         write(l, string'(" "));
         write(l, to_integer(unsigned(p_hc)));
         write(l, string'(" "));
         write(l, count28);
         writeline(ev, l);
         if kind = 2 then nr_write(n_reg, n_new);
         elsif kind = 4 then p57_write(n_new);
         elsif kind = 3 then p5b_write(n_new);
         elsif pal then pal_write(pal_idx, pal_new); else set_reg(r_idx, r_new); end if;
         wait until unsigned(p_vc) = to_unsigned(line_no + 2, 9) and unsigned(p_hc) = to_unsigned(0, 9);
         if kind = 2 then
            nr_write(n_reg, n_base);
         elsif kind = 4 then
            p303b_write(spr);
            if g_scenario = 45 then
               p57_write(std_logic_vector(to_unsigned(32 + (spr mod 16) * 16, 8)));
               p57_write(std_logic_vector(to_unsigned(32 + (spr / 16) * 16, 8)));
               p57_write(x"80");
            end if;
            p57_write(n_base);
         elsif kind = 3 then
            p303b_write(0);
            for i in 0 to pat_n loop p5b_write(n_base); end loop;
         elsif pal then pal_write(pal_idx, pal_base); else set_reg(r_idx, r_base); end if;
         k := k + 1;
      end loop;
      assert false report "sweep done" severity failure;
   end process;

   ----------------------------------------------------------------- log
   process (clk14)
      file f : text open write_mode is g_log;
      variable l : line;
   begin
      if rising_edge(clk14) then
         if to_integer(unsigned(p_vc)) >= 60 and to_integer(unsigned(p_vc)) <= 210 then
            write(l, count28);
            write(l, string'(" "));
            write(l, to_integer(unsigned(p_vc)));
            write(l, string'(" "));
            write(l, to_integer(unsigned(p_hc)));
            write(l, string'(" "));
            write(l, to_integer(unsigned(rgb)));
            write(l, string'(" "));
            if rgb_bk_n = '1' then write(l, string'("1")); else write(l, string'("0")); end if;
            writeline(f, l);
         end if;
      end if;
   end process;
end architecture;
