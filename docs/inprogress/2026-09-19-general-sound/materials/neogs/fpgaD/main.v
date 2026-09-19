// part of NeoGS project (c) 2007-2008 NedoPC
//
// main top-level module

module main(

	clk_fpga,  // clocks
	clk_24mhz, //

	clksel0, // clock selection
	clksel1, //

	warmres_n, // warm reset


	d, // Z80 data bus
	a, // Z80 address bus

	iorq_n,   // Z80 control signals
	mreq_n,   //
	rd_n,     //
	wr_n,     //
	m1_n,     //
	int_n,    //
	nmi_n,    //
	busrq_n,  //
	busak_n,  //
	z80res_n, //


	mema14,   // memory control
	mema15,   //
	mema16,   //
	mema17,   //
	mema18,   //
	ram0cs_n, //
	ram1cs_n, //
	ram2cs_n, //
	ram3cs_n, //
	romcs_n,  //
	memoe_n,  //
	memwe_n,  //


	zxid,        // zxbus signals
	zxa,         //
	zxa14,       //
	zxa15,       //
	zxiorq_n,    //
	zxmreq_n,    //
	zxrd_n,      //
	zxwr_n,      //
	zxcsrom_n,   //
	zxblkiorq_n, //
	zxblkrom_n,  //
	zxgenwait_n, //
	zxbusin,     //
	zxbusena_n,  //


	dac_bitck, // audio-DAC signals
	dac_lrck,  //
	dac_dat,  //


	sd_clk, // SD card interface
	sd_cs,  //
	sd_do,  //
	sd_di,  //
	sd_wp,  //
	sd_det, //


	ma_clk, // control interface of MP3 chip
	ma_cs,
	ma_do,
	ma_di,

	mp3_xreset, // data interface of MP3 chip
	mp3_req,    //
	mp3_clk,    //
	mp3_dat,    //
	mp3_sync,   //

	led_diag // LED driver

);


// input-output description

	input clk_fpga;
	input clk_24mhz;

	output clksel0;
	output clksel1;


	input warmres_n;

	inout reg [7:0] d;
	input [15:0] a;

	input iorq_n;
	input mreq_n;
	input rd_n;
	input wr_n;
	input m1_n;
	output int_n;
	output nmi_n;
	output busrq_n;
	input busak_n;
	output reg z80res_n;


	output mema14;
	output mema15;
	output mema16;
	output mema17;
	output mema18;
	output ram0cs_n;
	output ram1cs_n;
	output ram2cs_n;
	output ram3cs_n;
	output romcs_n;
	output memoe_n;
	output memwe_n;


	inout [7:0] zxid;
	input [7:0] zxa;
	input zxa14;
	input zxa15;
	input zxiorq_n;
	input zxmreq_n;
	input zxrd_n;
	input zxwr_n;
	input zxcsrom_n;
	output zxblkiorq_n;
	output zxblkrom_n;
	output zxgenwait_n;
	output zxbusin;
	output zxbusena_n;


	output dac_bitck;
	output dac_lrck;
	output dac_dat;


	output sd_clk;
	output sd_cs;
	output sd_do;
	input sd_di;
	input sd_wp;
	input sd_det;


	output ma_clk;
	output ma_cs;
	output ma_do;
	input ma_di;

	output mp3_xreset;
	input mp3_req;
	output mp3_clk;
	output mp3_dat;
	output mp3_sync;

	output led_diag;


// global signals

	wire internal_reset_n; // internal reset for everything


// zxbus-ports interconnection

	wire rst_from_zx_n; // internal z80 reset

	wire [7:0] command_zx2gs;
	wire [7:0] data_zx2gs;
	wire [7:0] data_gs2zx;
	wire command_bit_2gs;
	wire command_bit_2zx;
	wire command_bit_wr;
	wire data_bit_2gs;
	wire data_bit_2zx;
	wire data_bit_wr;

// ports-memmap interconnection
	wire mode_ramro,mode_norom;
	wire [6:0] mode_pg0,mode_pg1;

// ports databus
	wire [7:0] ports_dout;
	wire ports_busin;

// ports-sound interconnection
	wire snd_wrtoggle;
	wire snd_datnvol;
	wire [2:0] snd_addr;
	wire [7:0] snd_data;

	wire mode_8chans;
	wire mode_pan4ch;

// ports-SPIs interconnection

	wire [7:0] md_din;
	wire [7:0] mc_din;
	wire [7:0] mc_dout;
	wire [7:0] sd_din;
	wire [7:0] sd_dout;

	wire mc_start;
	wire [1:0] mc_speed;
	wire mc_rdy;

	wire md_start;
	wire md_halfspeed;

	wire sd_start;


// LED related

	wire led_toggle;






// CODE STARTS

// reset handling

	resetter my_rst( .clk(clk_fpga),
	                 .rst_in1_n( warmres_n ), .rst_in2_n( rst_from_zx_n ),
	                 .rst_out_n( internal_reset_n ) );

	always @* // reset for Z80
	begin
		if( internal_reset_n == 1'b0 )
			z80res_n <= 1'b0;
		else
			z80res_n <= 1'bZ;
	end




// control Z80 data bus

	always @*
	begin
		if( (!m1_n) && (!iorq_n) )
		begin
			d <= 8'hFF;
		end
		else
		begin
			if( ports_busin==1'b1 ) // FPGA inputs on data bus
				d <= 8'bZZZZZZZZ;
			else // FPGA outputs
				d <= ports_dout;
		end
	end

// control /BUSRQ

	assign busrq_n = 1'b1;





// ZXBUS module

	zxbus my_zxbus( .cpu_clock(clk_fpga),
	                .rst_n(internal_reset_n),
	                .rst_from_zx_n(rst_from_zx_n),

	                .nmi_n(nmi_n),

	                .zxid(zxid),
	                .zxa(zxa),
	                .zxa14(zxa14),
	                .zxa15(zxa15),
	                .zxiorq_n(zxiorq_n),
	                .zxmreq_n(zxmreq_n),
	                .zxrd_n(zxrd_n),
	                .zxwr_n(zxwr_n),
	                .zxblkiorq_n(zxblkiorq_n),
	                .zxblkrom_n(zxblkrom_n),
	                .zxcsrom_n(zxcsrom_n),
	                .zxgenwait_n(zxgenwait_n),
	                .zxbusin(zxbusin),
	                .zxbusena_n(zxbusena_n),

	                .command_reg_out(command_zx2gs),
	                .data_reg_out(data_zx2gs),
	                .data_reg_in(data_gs2zx),
	                .command_bit(command_bit_2gs),
	                .command_bit_in(command_bit_2zx),
	                .command_bit_wr(command_bit_wr),
	                .data_bit(data_bit_2gs),
	                .data_bit_in(data_bit_2zx),
	                .data_bit_wr(data_bit_wr),

	                .led_toggle(led_toggle) );



// MEMMAP module

	memmap my_memmap( .a14(a[14]),
	                  .a15(a[15]),
	                  .mreq_n(mreq_n),
	                  .rd_n(rd_n),
	                  .wr_n(wr_n),
	                  .mema14(mema14),
	                  .mema15(mema15),
	                  .mema16(mema16),
	                  .mema17(mema17),
	                  .mema18(mema18),

	                  .ram0cs_n(ram0cs_n),
	                  .ram1cs_n(ram1cs_n),
	                  .ram2cs_n(ram2cs_n),
	                  .ram3cs_n(ram3cs_n),
	                  .romcs_n(romcs_n),
	                  .memoe_n(memoe_n),
	                  .memwe_n(memwe_n),

	                  .mode_ramro(mode_ramro),
	                  .mode_norom(mode_norom),
	                  .mode_pg0(mode_pg0),
	                  .mode_pg1(mode_pg1) );



// PORTS module

	ports my_ports( .dout(ports_dout),
	                .din(d),
	                .busin(ports_busin),
	                .a(a),
	                .iorq_n(iorq_n),
	                .mreq_n(mreq_n),
	                .rd_n(rd_n),
	                .wr_n(wr_n),

	                .rst_n(internal_reset_n),

	                .cpu_clock(clk_fpga),

	                .clksel0(clksel0),
	                .clksel1(clksel1),

	                .snd_wrtoggle(snd_wrtoggle),
	                .snd_datnvol(snd_datnvol),
	                .snd_addr(snd_addr),
	                .snd_data(snd_data),
	                .mode_8chans(mode_8chans),
	                .mode_pan4ch(mode_pan4ch),

	                .command_port_input(command_zx2gs),
	                .command_bit_input(command_bit_2gs),
	                .command_bit_output(command_bit_2zx),
	                .command_bit_wr(command_bit_wr),
	                .data_port_input(data_zx2gs),
	                .data_port_output(data_gs2zx),
	                .data_bit_input(data_bit_2gs),
	                .data_bit_output(data_bit_2zx),
	                .data_bit_wr(data_bit_wr),

	                .mode_ramro(mode_ramro),
	                .mode_norom(mode_norom),
	                .mode_pg0(mode_pg0),
	                .mode_pg1(mode_pg1),

	                .md_din(md_din),
	                .md_start(md_start),
	                .md_dreq(mp3_req),
	                .md_halfspeed(md_halfspeed),

	                .mc_ncs(ma_cs),
	                .mc_xrst(mp3_xreset),
	                .mc_dout(mc_dout),
	                .mc_din(mc_din),
	                .mc_start(mc_start),
	                .mc_speed(mc_speed),
	                .mc_rdy(mc_rdy),

	                .sd_ncs(sd_cs),
	                .sd_wp(sd_wp),
	                .sd_det(sd_det),
	                .sd_din(sd_din),
	                .sd_dout(sd_dout),
	                .sd_start(sd_start),

	                .led(led_diag),
	                .led_toggle(led_toggle)


	                );



// SOUND_MAIN module

	sound_main my_sound_main( .clock(clk_24mhz),

	                          .mode_8chans(mode_8chans),
	                          .mode_pan4ch(mode_pan4ch),

	                          .in_wrtoggle(snd_wrtoggle),
	                          .in_datnvol(snd_datnvol),
	                          .in_wraddr(snd_addr),
	                          .in_data(snd_data),

	                          .dac_clock(dac_bitck),
	                          .dac_leftright(dac_lrck),
	                          .dac_data(dac_dat) );



// INTERRUPTS module

	interrupts my_interrupts( .clk_24mhz(clk_24mhz),
	                          .clk_z80(clk_fpga),

	                          .m1_n(m1_n),
	                          .iorq_n(iorq_n),

	                          .int_n(int_n) );







// MP3, SDcard spi modules


	spi2 spi_mp3_data( .clock(clk_fpga),
	                   .sck(mp3_clk),
	                   .sdo(mp3_dat),
	                   .bsync(mp3_sync),
	                   .din(md_din),
	                   .start(md_start),
	                   .speed( {1'b0,md_halfspeed} ),
	                   .sdi(1'b0) );

	spi2 spi_mp3_control( .clock(clk_fpga),
	                      .sck(ma_clk),
	                      .sdo(ma_do),
	                      .sdi(ma_di),
	                      .din(mc_din),
	                      .dout(mc_dout),
	                      .start(mc_start),
	                      .rdy(mc_rdy),
	                      .speed(mc_speed) );

	spi2 spi_sd( .clock(clk_fpga),
	             .sck(sd_clk),
	             .sdo(sd_do),
	             .sdi(sd_di),
	             .din(sd_din),
	             .dout(sd_dout),
	             .start(sd_start),
	             .speed(2'b00) );




endmodule

