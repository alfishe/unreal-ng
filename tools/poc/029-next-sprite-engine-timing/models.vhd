library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity sdpram_128_8 is port(DPRA:in std_logic_vector(6 downto 0); CLK,WE:in std_logic; DPO:out std_logic_vector(7 downto 0); A:in std_logic_vector(6 downto 0):=(others=>'0'); D:in std_logic_vector(7 downto 0):=(others=>'0')); end;
architecture m of sdpram_128_8 is type t is array(0 to 127) of std_logic_vector(7 downto 0); signal mem:t:=(others=>(others=>'0')); begin
 process(CLK) begin if rising_edge(CLK) then if WE='1' then mem(to_integer(unsigned(A)))<=D; end if; end if; end process;
 DPO<=mem(to_integer(unsigned(DPRA))); end;
library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity spram_320_9 is port(CLK,WE:in std_logic; SPO:out std_logic_vector(8 downto 0); A:in std_logic_vector(8 downto 0):=(others=>'0'); D:in std_logic_vector(8 downto 0):=(others=>'0')); end;
architecture m of spram_320_9 is type t is array(0 to 511) of std_logic_vector(8 downto 0); signal mem:t:=(others=>(others=>'0')); begin
 process(CLK) begin if rising_edge(CLK) then if WE='1' then mem(to_integer(unsigned(A)))<=D; end if; end if; end process;
 SPO<=mem(to_integer(unsigned(A))); end;
library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity sdpbram_16k_8 is port(WEA:in std_logic_vector(0 downto 0); ADDRA:in std_logic_vector(13 downto 0); DINA:in std_logic_vector(7 downto 0); CLKA:in std_logic; ENB:in std_logic; ADDRB:in std_logic_vector(13 downto 0); DOUTB:out std_logic_vector(7 downto 0); CLKB:in std_logic); end;
architecture m of sdpbram_16k_8 is begin
 -- constant content: byte = 1 + pattern number (addr 13:8)
 process(CLKB) begin if rising_edge(CLKB) then DOUTB<=std_logic_vector(to_unsigned(1+to_integer(unsigned(ADDRB(13 downto 8))),8)); end if; end process;
end;
