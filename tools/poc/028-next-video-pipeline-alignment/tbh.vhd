library ieee;
use ieee.std_logic_1164.all;
package tbh is
   -- POC 028 test hooks: forced register values (t_en bit n enables value byte n) and probes
   signal t_en    : std_logic_vector(63 downto 0) := (others => '0');
   signal t_val   : std_logic_vector(511 downto 0) := (others => '0');
   signal t_hold  : std_logic := '0';
   signal p_hc    : std_logic_vector(8 downto 0);
   signal p_vc    : std_logic_vector(8 downto 0);
   signal p_dbg   : std_logic_vector(15 downto 0);
end package;
