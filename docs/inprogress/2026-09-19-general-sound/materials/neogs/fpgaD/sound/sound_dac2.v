// part of NeoGS project (c) 2007-2008 NedoPC
//
// sound_dac is a serializer of 16 bit data for TDA1543 DAC
// input is clock at 24 MHz and 16-bit parallel data
// the module generates strobe signal for data sender - load, indicating when it loaded new portion of data,
// and allowing sender to begin preparing new data. dac_leftright is also to be used by sender (by locking it
// when load is 1).
//
// dac_clock is symmetrical clock at 1/10 of input clock (2.4 MHz)
// load is positive 1 clock cycle pulse informing that new data has just loaded

module sound_dac(
	clock,         // input clock (24 MHz)

	dac_clock,     // clock to DAC
	dac_leftright, // left-right signal to DAC (0 - left, 1 - right)
	dac_data,      // data to DAC

	load,          // output indicating cycle when new data loaded from datain bus

	datain         // input 16-bit bus
);

	input clock;

	output dac_clock;

	output dac_leftright;

	output dac_data;

	output reg load;


	input [15:0] datain;

	reg [16:0] data; // internal shift register

	reg [2:0] fifth; // divide by 5
	reg [6:0] sync; // sync[0] - dac_clock
	                // sync[6] - dac_leftright

	wire load_int;



	// for simulation purposes
	initial
	begin
		fifth <= 0;
		sync <= 0;
		data <= 0;
		load <= 0;
	end


	// divide input clock by 5
	always @(posedge clock)
	begin
		if( fifth[2] )
			fifth <= 3'b000;
		else
			fifth <= fifth + 3'd1;
	end

	// divide further to get dac_clock and dac_leftright
	always @(posedge clock)
	begin
		if( fifth[2] )  sync <= sync + 7'd1;
	end


	// load signal generation
	assign load_int = fifth[2] & (&sync[5:0]);

	always @(posedge clock)
	begin
			load <= load_int;
	end

	// loading and shifting data
	always @(posedge clock)
	begin
		if( fifth[2] && sync[0] )
		begin
			if( load_int )
				data[15:0] <= datain;
			else
				data[15:0] <= { data[14:0], 1'b0 }; // MSB first

			data[16] <= data[15];
		end
	end

	assign dac_leftright = sync[6];
	assign dac_clock = sync[0];
	assign dac_data  = data[16];

endmodule

