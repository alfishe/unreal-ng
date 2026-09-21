// part of NeoGS project (c) 2007-2008 NedoPC
//

// ports $00-$3f are in FPGA, $40-$ff are in CPLD

module ports(

	din,  // NGS z80 cpu DATA BUS inputs
	dout, // NGS z80 cpu DATA BUS outputs
	busin, // direction of bus: =1 - input, =0 - output
	a, // NSG z80 cpu ADDRESS BUS

	iorq_n,mreq_n,rd_n,wr_n, // NGS z80 cpu control signals


	data_port_input, // data_port input from zxbus module (async)
	data_port_output, // data_port output to zxbus module (async to zxbus, sync here)
	command_port_input, // command_port input from zxbus (async)

	data_bit_input, // data_bit from zxbus module (sync)
	command_bit_input, // --//-- (sync)

	data_bit_output, // output to zxbus module
	command_bit_output,

	data_bit_wr, // strobes (positive) to zxbus module, synchronous
	command_bit_wr,


	mode_8chans, // mode outputs for sound_main module
	mode_pan4ch, //

	mode_ramro, // mode outputs for memmap module
	mode_norom,

	mode_pg0, // page registers for memmap module
	mode_pg1,


	clksel0, // clock select (output from FPGA)
	clksel1,


	snd_wrtoggle, // toggle to write sound data to sound system memory
	snd_datnvol,  // whether it's for volume (=0) or for samples (=1)
	snd_addr,     // address: which channel to be written (0-7)
	snd_data,     // actual 8-bit data to be written


	md_din, // mp3 data interface
	md_start,
	md_dreq,
	md_halfspeed,

	mc_ncs, // mp3 control interface
	mc_xrst,
	mc_dout,
	mc_din,
	mc_start,
	mc_speed,
	mc_rdy,

	sd_ncs, // SD card interface
	sd_dout,
	sd_din,
	sd_start,
	sd_det,
	sd_wp,

	led, // LED control
	led_toggle,


	rst_n,

	cpu_clock // Z80 CPU clock (clk_fpga on schematics)
);


	localparam MPAG      = 6'h00;
	localparam MPAGEX    = 6'h10;

	localparam ZXCMD     = 6'h01;
	localparam ZXDATRD   = 6'h02;
	localparam ZXDATWR   = 6'h03;
	localparam ZXSTAT    = 6'h04;
	localparam CLRCBIT   = 6'h05;

	localparam VOL1      = 6'h06;
	localparam VOL2      = 6'h07;
	localparam VOL3      = 6'h08;
	localparam VOL4      = 6'h09;
	localparam VOL5      = 6'h16;
	localparam VOL6      = 6'h17;
	localparam VOL7      = 6'h18;
	localparam VOL8      = 6'h19;

	localparam DAMNPORT1 = 6'h0a;
	localparam DAMNPORT2 = 6'h0b;

	localparam LEDCTR    = 6'h01;

	localparam GSCFG0    = 6'h0f;

	localparam SCTRL     = 6'h11;
	localparam SSTAT     = 6'h12;

	localparam SD_SEND   = 6'h13;
	localparam SD_READ   = 6'h13;
	localparam SD_RSTR   = 6'h14;

	localparam MD_SEND   = 6'h14; // same as SD_RSTR!!!

	localparam MC_SEND   = 6'h15;
	localparam MC_READ   = 6'h15;



	// inputs/outputs description

	input      [7:0] din;
	output reg [7:0] dout;

	output reg busin; // =1 - dbus ins, =0 - dbus outs

	input [15:0] a;

	input iorq_n,mreq_n,rd_n,wr_n;

	input      [7:0] data_port_input;
	input      [7:0] command_port_input;
	output reg [7:0] data_port_output;

	input data_bit_input;
	input command_bit_input;

	output reg data_bit_output;

	output reg command_bit_output;

	output reg data_bit_wr;

	output reg command_bit_wr;

	output reg mode_8chans;

	output reg mode_pan4ch;

	output reg mode_ramro;

	output reg mode_norom;

	output reg [6:0] mode_pg0;
	output reg [6:0] mode_pg1;

	output reg clksel0;
	output reg clksel1;


	output reg snd_wrtoggle;
	output reg snd_datnvol;
	output reg [2:0] snd_addr;
	output reg [7:0] snd_data;


	input rst_n;

	input cpu_clock;




	// SPI interfaces related

	// MP3 data interface
	output [7:0] md_din; // data to MP3 data SPI interface

	output md_start; // start toggle for mp3 data spi

	input md_dreq; // data request from mp3 decoder

	output reg md_halfspeed;


	// MP3 control interface
	output reg mc_ncs; // nCS signal

	output reg mc_xrst; // xRESET signal

	output mc_start; // start toggle

	output reg [1:0] mc_speed;
	
	input mc_rdy;

	output [7:0] mc_din; // data to send

	input [7:0] mc_dout; // received data


      // SDcard interface
	output reg sd_ncs;

	output sd_start;

	output [7:0] sd_din;

	input [7:0] sd_dout;

	input sd_det;

	input sd_wp;


// LED control register

	output reg led;
	input led_toggle;



// internal regs & wires

	reg mode_expag; // extended paging mode register

	reg port09_bit5;

	wire port_enabled; // =1 when port address is in enabled region ($00-$3f)
	wire mem_enabled; // =1 when memory mapped sound regs are addressed ($6000-$7FFF)
	reg volports_enabled; // when volume ports are addressed (6-9 and $16-$19)

	reg iowrn_reg; // registered io write signal (all positive edge!)
	reg iordn_reg; // --//--
	reg merdn_reg; // --//--


	reg port_wr; // synchronous positive write pulse (write from z80 to fpga regs)
	reg port_rd;  // synchronous positive read pulse (read done from fpga regs to z80)

	reg memreg_rd; // when memory-mapped sound regs are read






	wire port00_wr;   // specific write and read strobes (1 clock cycle long positive)
	wire p_ledctr_wr;
	wire port02_rd;
	wire port03_wr;
	wire port05_wrrd;
	wire port09_wr;
	wire port0a_wrrd;
	wire port0b_wrrd;
	wire port0f_wr;
	wire port10_wr;

	wire p_sstat_rd;
	wire p_sctrl_rd;
	wire p_sctrl_wr;
	wire p_sdsnd_wr;
	wire p_sdrd_rd;
	wire p_sdrst_rd;
	wire p_mdsnd_wr;
	wire p_mcsnd_wr;
	wire p_mcrd_rd;

	reg [2:0] volnum; // volume register number from port address




// actual code

	//enabled ports
	assign port_enabled = ~(a[7] | a[6]); // $00-$3F

	//enabled mem
	assign mem_enabled = (~a[15]) & a[14] & a[13]; // $6000-$7FFF

	// volume ports enabled
	always @*
	begin
		if( a[5:0]==VOL1 ||
		    a[5:0]==VOL2 ||
		    a[5:0]==VOL3 ||
		    a[5:0]==VOL4 ||
		    a[5:0]==VOL5 ||
		    a[5:0]==VOL6 ||
		    a[5:0]==VOL7 ||
		    a[5:0]==VOL8 )

			volports_enabled <= 1'b1;
		else
			volports_enabled <= 1'b0;
	end



	//when data bus outputs
	always @*
	begin
		if( port_enabled && (!iorq_n) && (!rd_n) )
			busin <= 1'b0; // bus outputs
		else
			busin <= 1'b1; // bus inputs
	end



	// rd/wr/iorq syncing in and pulses
	always @(posedge cpu_clock)
	begin
		iowrn_reg <= iorq_n | wr_n;
		iordn_reg <= iorq_n | rd_n;

		if( port_enabled && (!iorq_n) && (!wr_n) && iowrn_reg )
			port_wr <= 1'b1;
		else
			port_wr <= 1'b0;

		if( port_enabled && (!iorq_n) && (!rd_n) && iordn_reg )
			port_rd <= 1'b1;
		else
			port_rd <= 1'b0;

	end

	// mreq syncing and mem read pulse
	always @(negedge cpu_clock)
	begin
		merdn_reg <= mreq_n | rd_n;

		if( mem_enabled && (!mreq_n) && (!rd_n) && merdn_reg )
			memreg_rd <= 1'b1;
		else
			memreg_rd <= 1'b0;

	end


	// specific ports strobes
	assign port00_wr   = ( a[5:0]==MPAG      && port_wr            );
	assign port02_rd   = ( a[5:0]==ZXDATRD   && port_rd            );
	assign port03_wr   = ( a[5:0]==ZXDATWR   && port_wr            );
	assign port05_wrrd = ( a[5:0]==CLRCBIT   && (port_wr||port_rd) );
	assign port09_wr   = ( a[5:0]==VOL4      && port_wr            );
	assign port0a_wrrd = ( a[5:0]==DAMNPORT1 && (port_wr||port_rd) );
	assign port0b_wrrd = ( a[5:0]==DAMNPORT2 && (port_wr||port_rd) );
	assign port0f_wr   = ( a[5:0]==GSCFG0    && port_wr            );
	assign port10_wr   = ( a[5:0]==MPAGEX    && port_wr            );


	assign p_sctrl_rd = ( a[5:0]==SCTRL  && port_rd );
	assign p_sctrl_wr = ( a[5:0]==SCTRL  && port_wr );
	assign p_sstat_rd = ( a[5:0]==SSTAT  && port_rd );
	assign p_sdsnd_wr = ( a[5:0]==SD_SEND && port_wr );
	assign p_sdrd_rd  = ( a[5:0]==SD_READ && port_rd );
	assign p_sdrst_rd = ( a[5:0]==SD_RSTR && port_rd );
	assign p_mdsnd_wr = ( a[5:0]==MD_SEND && port_wr );
	assign p_mcsnd_wr = ( a[5:0]==MC_SEND && port_wr );
	assign p_mcrd_rd  = ( a[5:0]==MC_READ && port_rd );

	assign p_ledctr_wr = ( a[5:0]==LEDCTR && port_wr );



	// read from fpga to Z80
	always @*
	begin
		case( a[5:0] )
		ZXCMD: // command register
			dout <= command_port_input;
		ZXDATRD: // data register
			dout <= data_port_input;
		ZXSTAT: // status bits
			dout <= { data_bit_input, 6'bXXXXXX, command_bit_input };
		GSCFG0: // config register #0F
			dout <= { 1'b0, mode_pan4ch, clksel1, clksel0, mode_expag, mode_8chans, mode_ramro, mode_norom };

		SSTAT:
			dout <= { 4'd0, mc_rdy, sd_wp, sd_det, md_dreq };
		SCTRL:
			dout <= { 2'd0, mc_speed[1], md_halfspeed, mc_speed[0], mc_xrst, mc_ncs, sd_ncs };
		SD_READ:
			dout <= sd_dout;
		SD_RSTR:
			dout <= sd_dout;
		MC_READ:
			dout <= mc_dout;

		default:
			dout <= 8'bXXXXXXXX;
		endcase
	end





	// write to $00 and $10 ports ++
	always @(posedge cpu_clock)
	begin
		if( port00_wr==1'b1 ) // port 00
		begin
			if( mode_expag==1'b0 ) // normal paging
				mode_pg0[6:0] <= { din[5:0], 1'b0 };
			else // extended paging
				mode_pg0[6:0] <= { din[5:0], din[7] };
		end

		if( mode_expag==1'b0 && port00_wr==1'b1 ) // port 10 (when in normal mode, part of port 00)
			mode_pg1[6:0] <= { din[5:0], 1'b1 };
		else if( mode_expag==1'b1 && port10_wr==1'b1 )
			mode_pg1[6:0] <= { din[5:0], din[7] };
	end

	// port $03 write ++
	always @(posedge cpu_clock)
	begin
		if( port03_wr==1'b1 )
			data_port_output <= din;
	end

	// port $09 bit tracing
	always @(posedge cpu_clock)
	begin
		if( port09_wr==1'b1 )
			port09_bit5 <= din[5];
	end

	// write and reset of port $0F ++
	always @(posedge cpu_clock,negedge rst_n)
	begin
		if( rst_n==1'b0 ) // reset!
			{ mode_pan4ch, clksel1, clksel0, mode_expag, mode_8chans, mode_ramro, mode_norom } <= 7'b0110000;
		else // write to port
		begin
			if( port0f_wr == 1'b1 )
			begin
				{ mode_pan4ch, clksel1, clksel0, mode_expag, mode_8chans, mode_ramro, mode_norom } <= din[6:0];
			end
		end
	end

	// data bit handling
    always @*
    begin
		case( {port02_rd,port03_wr,port0a_wrrd} )
		3'b100:
		begin
			data_bit_output <= 1'b0;
			data_bit_wr <= 1'b1;
		end

		3'b010:
		begin
			data_bit_output <= 1'b1; // ++
			data_bit_wr <= 1'b1;
		end

		3'b001:
		begin
			data_bit_output <= ~mode_pg0[0];
			data_bit_wr <= 1'b1;
		end

		default:
		begin
			data_bit_output <= 1'bX;
			data_bit_wr <= 1'b0;
		end
    	endcase

    end

	// command bit handling
	always @*
	begin
		casex( {port05_wrrd,port0b_wrrd} )
		2'b10:
		begin
			command_bit_output <= 1'b0;
			command_bit_wr <= 1'b1;
		end

		2'b01:
		begin
			command_bit_output <= port09_bit5;
			command_bit_wr <= 1'b1;
		end

		default:
		begin
			command_bit_output <= 1'bX;
			command_bit_wr <= 1'b0;
		end
		endcase
	end

	// handle data going to sound module (volume and samples values)
	always @*
	begin
		case( a[5:0] ) // port addresses to volume register numbers
		VOL1:
			volnum <= 3'd0;
		VOL2:
			volnum <= 3'd1;
		VOL3:
			volnum <= 3'd2;
		VOL4:
			volnum <= 3'd3;
		VOL5:
			volnum <= 3'd4;
		VOL6:
			volnum <= 3'd5;
		VOL7:
			volnum <= 3'd6;
		VOL8:
			volnum <= 3'd7;
		default:
			volnum <= 3'bXXX;
		endcase
	end

	// handling itself (sending data to sound module)
	always @(posedge cpu_clock)
	begin
		if( memreg_rd ) // memory read - sample data write
		begin
			snd_wrtoggle <= ~snd_wrtoggle;
			snd_datnvol  <= 1'b1; // sample data

			if( !mode_8chans ) // 4 channel mode
				snd_addr <= { 1'b0, a[9:8] };
			else // 8 channel mode
				snd_addr <= a[10:8];

			snd_data <= din;
		end
		else if( volports_enabled && port_wr )
		begin
			snd_wrtoggle <= ~snd_wrtoggle;
			snd_datnvol  <= 1'b0; // volume data
			snd_addr <= volnum;
			snd_data <= din;
		end
	end






	//SPI (mp3, SD) interfaces

	assign sd_din = (a[5:0]==SD_RSTR) ? 8'hFF : din;
	assign mc_din = din;
	assign md_din = din;


	assign sd_start = p_sdsnd_wr | p_sdrst_rd;
	assign mc_start = p_mcsnd_wr;
	assign md_start = p_mdsnd_wr;


      always @(posedge cpu_clock, negedge rst_n)
      begin
		if( !rst_n ) // async reset
		begin
			md_halfspeed <= 1'b0;
			mc_speed     <= 2'b01;
			mc_xrst      <= 1'b0;
			mc_ncs       <= 1'b1;
			sd_ncs       <= 1'b1;
		end
		else // clock
		begin
			if( p_sctrl_wr )
			begin
				if( din[0] )
					sd_ncs       <= din[7];

				if( din[1] )
					mc_ncs       <= din[7];

				if( din[2] )
					mc_xrst      <= din[7];

				if( din[3] )
					mc_speed[0]  <= din[7];

				if( din[4] )
					md_halfspeed <= din[7];
				
				if( din[5] )
					mc_speed[1]  <= din[7];

			end
		end
      end



	// LED control
	always @(posedge cpu_clock, negedge rst_n)
	begin
		if( !rst_n )
			led <= 1'b0;
		else
		begin
			if( p_ledctr_wr )
				led <= din[0];
			else if( led_toggle )
				led <= ~led;
		end

	end






endmodule

