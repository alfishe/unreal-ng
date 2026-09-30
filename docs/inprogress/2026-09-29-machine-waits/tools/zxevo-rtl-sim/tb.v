// Wrapper: zclock + zmem + arbiter + sync_h/v from BaseConf RTL.
`include "include/tune.v"
module tb(
	input  wire        fclk,
	input  wire        rst_n,
	// z80 bus (driven by C++ model)
	input  wire [15:0] a,
	input  wire        mreq_n, iorq_n, m1_n, rfsh_n, rd_n, wr_n,
	input  wire [ 1:0] turbo,
	input  wire [ 1:0] modes_raster,
	input  wire [ 2:0] p7ffd,
	input  wire [ 1:0] go_mode,   // 0: go=0, 1: go=1, 2: real fetch window
	input  wire [ 1:0] bw,
	input  wire        win0_rom,  // window 0 is ROM
	input  wire        contend_type,
	output wire        zclk_out,
	output wire        zpos, zneg,
	output wire        int_n,
	output wire [ 1:0] int_turbo,
	output wire        contend,
	output wire        cpu_stall,
	output wire        stall_total,
	output wire [ 8:0] hcount_o,
	output wire [ 8:0] vcount_o,
	output wire        vpix_o,
	output wire        cend_o, pre_cend_o, post_cbeg_o, cbeg_o,
	output wire [3:0]  dbg_o,
	output wire        cpu_next_o, cpu_strobe_o, dram_beg_o, go_o
);
	reg [1:0] cc = 2'd0;
	always @(posedge fclk) cc <= cc + 2'd1;
	wire cbeg = (cc == 2'd0);

	wire cend, pre_cend, post_cbeg;
	wire cpu_req, cpu_rnw, cpu_wrbsel, cpu_next, cpu_strobe;
	wire [20:0] cpu_addr; wire [7:0] cpu_wrdata; wire [15:0] cpu_rddata;
	wire [15:0] video_data; wire video_strobe, video_next;
	wire [20:0] dram_addr; wire dram_req, dram_rnw; wire [1:0] dram_bsel; wire [15:0] dram_wrdata;

	reg go_real = 1'b0;
	wire fetch_start, fetch_end, vpix;
	always @(posedge fclk)
		if (fetch_start && vpix) go_real <= 1'b1; else if (fetch_end) go_real <= 1'b0;
	wire go = (go_mode == 2'd0) ? 1'b0 : (go_mode == 2'd1) ? 1'b1 : go_real;
	assign go_o = go;

	arbiter arbiter(
		.clk(fclk), .rst_n(rst_n),
		.dram_addr(dram_addr), .dram_req(dram_req), .dram_rnw(dram_rnw),
		.dram_cbeg(cbeg), .dram_rrdy(1'b0), .dram_bsel(dram_bsel),
		.dram_rddata(16'h0000), .dram_wrdata(dram_wrdata),
		.cend(cend), .pre_cend(pre_cend), .post_cbeg(post_cbeg),
		.go(go), .bw(bw),
		.video_addr(21'd0), .video_data(video_data), .video_strobe(video_strobe), .video_next(video_next),
		.cpu_req(cpu_req), .cpu_rnw(cpu_rnw), .cpu_addr(cpu_addr), .cpu_wrdata(cpu_wrdata),
		.cpu_wrbsel(cpu_wrbsel), .cpu_rddata(cpu_rddata), .cpu_next(cpu_next), .cpu_strobe(cpu_strobe)
	);

	wire external_port = (a[7:0] == 8'hFD) && a[15];

	wire [7:0] zd_out; wire zd_ena; wire [4:0] rompg; wire romoe_n, romwe_n, csrom;

	zmem zmem(
		.fclk(fclk), .rst_n(rst_n), .zpos(zpos), .zneg(zneg),
		.cbeg(cbeg), .post_cbeg(post_cbeg), .pre_cend(pre_cend), .cend(cend),
		.za(a), .zd_in(8'h00), .zd_out(zd_out), .zd_ena(zd_ena),
		.m1_n(m1_n), .rfsh_n(rfsh_n), .mreq_n(mreq_n), .iorq_n(iorq_n), .rd_n(rd_n), .wr_n(wr_n),
		.int_turbo(int_turbo),
		.win0_romnram(win0_rom), .win1_romnram(1'b0), .win2_romnram(1'b0), .win3_romnram(1'b0),
		.win0_page(8'd0), .win1_page(8'd5), .win2_page(8'd2), .win3_page(8'd0),
		.win0_wrdisable(1'b0), .win1_wrdisable(1'b0), .win2_wrdisable(1'b0), .win3_wrdisable(1'b0),
		.romrw_en(1'b0), .nmi_buf_clr(1'b0),
		.rompg(rompg), .romoe_n(romoe_n), .romwe_n(romwe_n), .csrom(csrom),
		.cpu_req(cpu_req), .cpu_rnw(cpu_rnw), .cpu_addr(cpu_addr), .cpu_wrdata(cpu_wrdata),
		.cpu_wrbsel(cpu_wrbsel), .cpu_rddata(cpu_rddata), .cpu_next(cpu_next), .cpu_strobe(cpu_strobe),
		.cpu_stall(cpu_stall)
	);

	zclock zclock(
		.fclk(fclk), .rst_n(rst_n), .zclk(1'b0), .a(a),
		.modes_raster(modes_raster), .mode_contend_type(contend_type), .mode_contend_ena(1'b1),
		.mode_7ffd_bits(p7ffd), .contend(contend),
		.mreq_n(mreq_n), .iorq_n(iorq_n), .m1_n(m1_n), .rfsh_n(rfsh_n),
		.zclk_out(zclk_out), .zpos(zpos), .zneg(zneg),
		.zclk_stall(cpu_stall), .turbo(turbo), .int_turbo(int_turbo),
		.external_port(external_port), .cbeg(cbeg), .pre_cend(pre_cend)
	);
	assign stall_total = zclock.stall;

	wire hblank, hsync, line_start, hsync_start, hint_start, scanin_start, hpix, border_sync;
	video_sync_h video_sync_h(
		.clk(fclk), .init(1'b0), .cend(cend), .pre_cend(pre_cend),
		.mode_atm_n_pent(1'b0), .mode_a_text(1'b0),
		.modes_raster(modes_raster), .mode_contend_type(contend_type),
		.hblank(hblank), .hsync(hsync), .line_start(line_start), .hsync_start(hsync_start),
		.hint_start(hint_start), .scanin_start(scanin_start), .vpix(vpix), .hpix(hpix),
		.contend(contend), .border_sync(border_sync), .fetch_start(fetch_start), .fetch_end(fetch_end)
	);

	wire vblank, vsync, int_start;
	video_sync_v video_sync_v(
		.clk(fclk), .hsync_start(hsync_start), .line_start(line_start), .hint_start(hint_start),
		.mode_atm_n_pent(1'b0), .modes_raster(modes_raster),
		.vblank(vblank), .vsync(vsync), .int_start(int_start), .vpix(vpix)
	);

	assign int_n = ~int_start; // int_n (zint.v) falls one fclk after int_start

	assign hcount_o = video_sync_h.hcount;
	assign vcount_o = video_sync_v.vcount;
	assign vpix_o = vpix;
	assign cend_o = cend; assign pre_cend_o = pre_cend; assign post_cbeg_o = post_cbeg; assign cbeg_o = cbeg;
	assign dbg_o = {zmem.stall14_ini, zmem.stall14_cyc, zmem.stall14_fin, zmem.pending_cpu_req};
	assign cpu_next_o = cpu_next; assign cpu_strobe_o = cpu_strobe; assign dram_beg_o = zmem.dram_beg;
endmodule
