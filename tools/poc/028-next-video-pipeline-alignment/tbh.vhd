library ieee;
use ieee.std_logic_1164.all;
package tbh is
   -- POC 028 test hooks: forced register values (t_en bit n enables value byte n) and probes
   signal t_en    : std_logic_vector(63 downto 0) := (others => '0');
   signal t_val   : std_logic_vector(511 downto 0) := (others => '0');
   signal t_hold  : std_logic := '0';
   signal p_hc    : std_logic_vector(8 downto 0);
   signal p_vc    : std_logic_vector(8 downto 0);
   signal t_pal_we  : std_logic := '0';                        -- a palette write (NR #41 / #44 path)
   signal t_pal_val : std_logic_vector(8 downto 0) := (others => '0');
   signal t_pal_idx : std_logic_vector(7 downto 0) := (others => '0');
   -- injected NextREG writes (the decode of the real strobe) and sprite port writes (#57 attributes, #5B patterns)
   signal t_nr_we   : std_logic := '0';
   signal t_nr_reg  : std_logic_vector(7 downto 0) := (others => '0');
   signal t_nr_dat  : std_logic_vector(7 downto 0) := (others => '0');
   signal t_p57_we  : std_logic := '0';
   signal t_p5b_we  : std_logic := '0';
   signal t_p303b_we : std_logic := '0';
   signal t_p_dat   : std_logic_vector(7 downto 0) := (others => '0');
   signal p_dbg   : std_logic_vector(15 downto 0);
end package;
