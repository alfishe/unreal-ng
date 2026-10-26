-- POC 036 testbench: the whole zxnext core with the clocks and the SRAM arbiter of the top level, the T80 running a test ROM from a
-- real SRAM; logs the instruction starts, memory and port cycles and the INT in 28 MHz clocks
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use work.tbh.all;
use work.tbp.all;

entity tb_cpu is
   generic (
      g_nr03   : natural := 16#91#;  -- the NR #03 write that configures the machine (#91 48K, #A2 128K, #B3 +3)
      g_ints   : natural := 6;       -- stop at this INT (counted from 1 after the configuring reset)
      g_rom    : string  := "rom.hex";
      g_log    : string  := "trace.txt"
   );
end entity;

architecture sim of tb_cpu is
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

   signal zeros12 : std_logic_vector(11 downto 0) := (others => '0');
   signal zeros28 : std_logic_vector(27 downto 0) := (others => '0');
   signal kbd_col : std_logic_vector(4 downto 0) := (others => '1');

   signal count28 : natural := 0;
   signal configured : boolean := false;
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
         clk28 <= '1'; clk14 <= '1'; clk7 <= '1'; wait for T28 / 2; clk28 <= '0'; wait for T28 / 2;
         clk28 <= '1'; clk14 <= '0'; clk7 <= '1'; wait for T28 / 2; clk28 <= '0'; wait for T28 / 2;
         clk28 <= '1'; clk14 <= '1'; clk7 <= '0'; wait for T28 / 2; clk28 <= '0'; wait for T28 / 2;
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

   -- power-on reset, then (the boot ROM running) NR #03 configures the machine and leaves config mode, then a reset starts the
   -- CPU at 0 on the test ROM (NR #03 keeps its value over a reset, the boot ROM stays off out of config mode: zxnext.vhd 5108)
   process
   begin
      reset <= '1';
      wait for 40 * T28;
      reset <= '0';
      wait for 400 * T28;
      wait until rising_edge(clk28);
      t_nr_reg <= x"03"; t_nr_dat <= std_logic_vector(to_unsigned(g_nr03, 8)); t_nr_we <= '1';
      wait until rising_edge(clk28);
      t_nr_we <= '0';
      wait for 40 * T28;
      reset <= '1';
      wait for 40 * T28;
      reset <= '0';
      configured <= true;
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
         ram_addr_o <= sram_addr(21 downto 1);
         sram_oe_n_active <= sram_rd_n;
         sram_port_a_active <= ram_a_req;
         sram_port_b_active <= zxn_ram_b_req;
         sram_data_h_active <= sram_addr(0);
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

   -- the SRAM: ROM 0-3 at #00000, RAM page n at #40000 + n * #2000 (zxnext.vhd 2964); bank 5 and 7 are the core's own BRAM.
   -- A port A request with RD_n high is a write of the byte (the top level's sram_rd_n / we line); reads answer the word addressed
   process (clk28, ram_addr_o)
      type mem_t is array (0 to 16#5FFFF#) of integer range 0 to 255;
      variable mem : mem_t := (others => 0);
      variable loaded : boolean := false;
      file f : text;
      variable l : line;
      variable b : std_logic_vector(7 downto 0);
      variable a : natural;
      variable ok : boolean;
      variable wa : natural;
   begin
      if not loaded then
         file_open(f, g_rom, read_mode);
         a := 0;
         while not endfile(f) loop
            readline(f, l);
            hread(l, b, ok);
            if ok then
               mem(a) := to_integer(unsigned(b));
               a := a + 1;
            end if;
         end loop;
         file_close(f);
         loaded := true;
      end if;
      if rising_edge(clk28) then
         if ram_a_req = '1' and ram_a_rd_n = '1' then
            wa := to_integer(unsigned(ram_a_addr_21 & ram_a_addr));
            if wa <= mem'high then
               mem(wa) := to_integer(unsigned(ram_a_do));
            end if;
         end if;
      end if;
      wa := to_integer(unsigned(ram_addr_o)) * 2;
      if wa + 1 <= mem'high then
         ram_data <= std_logic_vector(to_unsigned(mem(wa + 1), 8)) & std_logic_vector(to_unsigned(mem(wa), 8));
      else
         ram_data <= x"FFFF";
      end if;
   end process;

   ----------------------------------------------------------------- log
   probes : block
   begin
      process (clk28)
         file f : text open write_mode is g_log;
         variable l : line;
         variable p_m1, p_mrd, p_mwr, p_ird, p_iwr, p_int : std_logic := '1';
         variable c_m1, c_mrd, c_mwr, c_ird, c_iwr : std_logic;
         variable ints : natural := 0;
         variable last_di : std_logic_vector(7 downto 0) := x"00";  -- the data bus a clock before the strobe ends
         procedure ev(k : string; with_a : boolean; v : std_logic_vector(7 downto 0)) is
         begin
            write(l, k);
            write(l, string'(" "));
            write(l, count28);
            write(l, string'(" "));
            write(l, to_integer(unsigned(p_vc)));
            write(l, string'(" "));
            write(l, to_integer(unsigned(p_hc)));
            if with_a then
               write(l, string'(" "));
               hwrite(l, q_cpu_a);
               write(l, string'(" "));
               hwrite(l, v);
               if q_dma_holds_bus = '1' then write(l, string'(" dma")); end if;
            end if;
            writeline(f, l);
         end procedure;
      begin
         if rising_edge(clk28) and configured then
            c_m1 := q_cpu_m1_n or q_cpu_mreq_n;
            c_mrd := q_cpu_mreq_n or q_cpu_rd_n or not q_cpu_m1_n;
            c_mwr := q_cpu_mreq_n or q_cpu_wr_n;
            c_ird := q_cpu_iorq_n or q_cpu_rd_n or not q_cpu_m1_n;
            c_iwr := q_cpu_iorq_n or q_cpu_wr_n;
            if p_m1 = '1' and c_m1 = '0' then ev("M", true, x"00"); end if;
            if p_m1 = '0' and c_m1 = '1' then ev("m", true, last_di); end if;  -- end of the fetch: the opcode
            if p_mrd = '0' and c_mrd = '1' then ev("R", true, last_di); end if;  -- the data at the end of the read
            if p_mwr = '1' and c_mwr = '0' then ev("W", true, q_cpu_do); end if;
            if p_ird = '0' and c_ird = '1' then ev("i", true, last_di); end if;
            if p_ird = '1' and c_ird = '0' then ev("I", true, x"00"); end if;
            if p_iwr = '1' and c_iwr = '0' then ev("O", true, q_cpu_do); end if;
            if p_int = '1' and q_z80_int_n = '0' then
               ints := ints + 1;
               ev("INT", false, x"00");
               if ints = g_ints then
                  assert false report "done" severity failure;
               end if;
            end if;
            p_m1 := c_m1; p_mrd := c_mrd; p_mwr := c_mwr; p_ird := c_ird; p_iwr := c_iwr; p_int := q_z80_int_n;
            last_di := q_cpu_di;
         end if;
      end process;
   end block;
end architecture;
