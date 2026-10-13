library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all; use std.textio.all;
entity tb is generic(g_scn:natural:=0; g_k:natural:=100; g_k2:natural:=0; g_tx:natural:=304; g_len:natural:=448;
  g_xa:natural:=10; g_xb:natural:=15; g_y:natural:=40; g_over:natural:=1; g_clipen:natural:=0;
  g_cx1:natural:=0; g_cx2:natural:=255; g_cy1:natural:=0; g_cy2:natural:=255; g_zot:natural:=0; g_probe:natural:=1); end;
architecture s of tb is
 signal clk,clkn,clk7:std_logic:='0'; signal phase:natural:=0;
 signal rst:std_logic:='1';
 signal hc,vc:unsigned(8 downto 0):=(others=>'0');
 signal pos:natural:=0;
 signal mwe:std_logic:='0'; signal midx:std_logic_vector(2 downto 0):="000"; signal mdat:std_logic_vector(7 downto 0):=(others=>'0');
 signal r303:std_logic:='0'; signal dout:std_logic_vector(7 downto 0);
 signal rgb:std_logic_vector(7 downto 0); signal pen:std_logic;
 signal done:boolean:=false;
 signal zot,bce,ob:std_logic;
begin
 zot <= '1' when g_zot=1 else '0';
 bce <= '1' when g_clipen=1 else '0';
 ob  <= '1' when g_over=1 else '0';
 process begin
  while not done loop
   clk<='1'; if phase<2 then clk7<='1'; else clk7<='0'; end if; wait for 5 ns;
   clk<='0'; wait for 5 ns; phase<=(phase+1) mod 4;
  end loop; wait; end process;
 clkn<=not clk;
 dut: entity work.sprites port map(clock_master_i=>clk, clock_master_180o_i=>clkn, clock_pixel_i=>clk7, reset_i=>rst,
  zero_on_top_i=>zot, border_clip_en_i=>bce, over_border_i=>ob,
  hcounter_i=>hc, vcounter_i=>vc, transp_colour_i=>x"E3",
  port57_w_en_s=>'0', port5B_w_en_s=>'0', port303b_r_en_s=>r303, port303b_w_en_s=>'0', cpu_d_i=>x"00", cpu_d_o=>dout,
  mirror_tie_i=>'0', mirror_we_i=>mwe, mirror_index_i=>midx, mirror_data_i=>mdat, mirror_inc_i=>'0', mirror_num_o=>open,
  rgb_o=>rgb, pixel_en_o=>pen,
  clip_x1_i=>to_unsigned(g_cx1,8), clip_x2_i=>to_unsigned(g_cx2,8), clip_y1_i=>to_unsigned(g_cy1,8), clip_y2_i=>to_unsigned(g_cy2,8));
 -- counters: hcounter starts at 496 (-16), L pixels per line, vc increments at line start
 process(clk7) begin if rising_edge(clk7) then
   if pos=g_len-1 then pos<=0; else pos<=pos+1; end if;
   hc<=to_unsigned((496+ (pos+1)) mod 512,9);
   if pos=g_len-1 then vc<=vc+1; end if;
 end if; end process;
 -- sampling at phase 3 of the pixel period (after the falling edge load)
 process
  variable nprobe,nall:natural:=0; variable mn:integer:=1000; variable mx:integer:=-1; variable ntun:natural:=0; variable l:line;
 begin
  loop
   wait until rising_edge(clk) and phase=3;
   if to_integer(vc)=g_y+5 and to_integer(hc)<320 then
     if pen='1' then nall:=nall+1;
       if rgb=x"03" then ntun:=ntun+1; end if; if rgb=x"02" then nprobe:=nprobe+1; if to_integer(hc)<mn then mn:=to_integer(hc); end if; if to_integer(hc)>mx then mx:=to_integer(hc);end if;
       end if;
     end if;
   end if;
   if to_integer(vc)=g_y+6 then
     write(l,string'("SAMPLE nall=")); write(l,nall); write(l,string'(" nprobe=")); write(l,nprobe);
     write(l,string'(" probe_x=")); write(l,mn); write(l,string'("..")); write(l,mx); write(l,string'(" ntuner=")); write(l,ntun); writeline(output,l);
     wait;
   end if;
  end loop;
 end process;
 -- control
 process
  procedure tick is begin wait until rising_edge(clk); end;
  procedure setspr(n:natural; x:natural; y:natural; vis:boolean; pat:natural; xsc:natural) is
   variable a2,a3,a4:std_logic_vector(7 downto 0);
  begin
   a2:=x"00"; if x>=256 then a2(0):='1'; end if;
   a3:="00"&std_logic_vector(to_unsigned(pat,6));
   if vis then a3(7):='1'; end if;
   a4:=x"00"; if xsc>0 then a3(6):='1'; a4(4 downto 3):=std_logic_vector(to_unsigned(xsc,2)); end if;
   mwe<='1'; midx<="111"; mdat<=std_logic_vector(to_unsigned(n,8)); tick; mwe<='0'; tick;
   mwe<='1'; midx<="000"; mdat<=std_logic_vector(to_unsigned(x mod 256,8)); tick; mwe<='0'; tick;
   mwe<='1'; midx<="001"; mdat<=std_logic_vector(to_unsigned(y mod 256,8)); tick; mwe<='0'; tick;
   mwe<='1'; midx<="010"; mdat<=a2; tick; mwe<='0'; tick;
   mwe<='1'; midx<="011"; mdat<=a3; tick; mwe<='0'; tick;
   mwe<='1'; midx<="100"; mdat<=a4; tick; mwe<='0'; tick;
  end;
  procedure rd303 is variable l:line; begin
   r303<='1'; for i in 1 to 8 loop tick; end loop; r303<='0'; for i in 1 to 4 loop tick; end loop;
   write(l,string'("STATUS bit1(too many)=")); write(l,std_logic'image(dout(1))); write(l,string'(" bit0(collision)=")); write(l,std_logic'image(dout(0)));
   writeline(output,l);
  end;
 begin
  for i in 1 to 8 loop tick; end loop; rst<='0';
  for i in 1 to 4 loop tick; end loop;
  if g_scn<2 then
   for i in 0 to 127 loop
    if i<g_k then setspr(i,0,g_y,true,0,0);
    elsif i<g_k+g_k2 then setspr(i,0,g_y,true,0,1);
    elsif i=126 then setspr(i,g_tx,g_y,true,2,0);
    elsif i=127 and g_probe=1 then setspr(i,288,g_y,true,1,0);
    else setspr(i,0,0,false,0,0); end if;
   end loop;
  else
   for i in 0 to 127 loop
    if i=0 then setspr(i,g_xa,g_y,true,1,0);
    elsif i=1 then setspr(i,g_xb,g_y,true,0,0);
    else setspr(i,0,0,false,0,0); end if;
   end loop;
  end if;
  wait until vc=10; tick; rd303;
  wait until vc=g_y+20; tick; rd303;
  done<=true; wait;
 end process;
end;
