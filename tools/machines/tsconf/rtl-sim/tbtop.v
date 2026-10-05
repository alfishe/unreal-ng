// Simulation top for the TS-Conf video pipeline:
// clock (common/clock.v) + arbiter (dram/arbiter.v) + video_top (video/*.v),
// as wired in top.v. The DRAM chip is replaced by a behavioral read model:
// the word address the arbiter presents at c3 (start of a DRAM cycle) is read
// from the C++ memory image (DPI) and held on the read-data bus for that whole
// cycle, so video_strobe (c3 at the end of the cycle) latches it. The CPU side is
// a single cpu_req input (idle or requesting every cycle) to exercise arbitration;
// no DMA or copper traffic. Register and SFILE writes come from the C++ testbench.
`include "tune.v"

module tbtop
(
  input  wire       clk,
  input  wire       res,
  input  wire [7:0] xt_wr_data,
  input  wire       border_wr,
  input  wire       vpage_wr,
  input  wire       vconf_wr,
  input  wire       gx_offsl_wr,
  input  wire       gx_offsh_wr,
  input  wire       gy_offsl_wr,
  input  wire       gy_offsh_wr,
  input  wire       palsel_wr,
  input  wire       tsconf_wr,
  input  wire       t0x_offsl_wr, t0x_offsh_wr, t0y_offsl_wr, t0y_offsh_wr,
  input  wire       t1x_offsl_wr, t1x_offsh_wr, t1y_offsl_wr, t1y_offsh_wr,
  input  wire       tmpage_wr, t0gpage_wr, t1gpage_wr, sgpage_wr,
  input  wire       sfile_we,    // SFILE word write: zma = word address, zmd = data
  input  wire [7:0] zma,
  input  wire [15:0] zmd,
  input  wire       cpu_req,     // 1 = CPU asks for every DRAM cycle (worst-case contention)

  output wire [8:0] ray_x,
  output wire [8:0] ray_y,
  output wire       c0_o, c1_o, c2_o, c3_o,
  output wire [7:0] vdata_o,     // palette index entering CRAM (video_out.vdata)
  output wire [7:0] vplex_o,     // video_render output (before the video_out c3 register)
  output wire       hvpix_o,
  output wire       tv_hires_o,
  // TSU observation (the tsulatch command)
  output wire       line_start_o,  // video_sync line_start_s
  output wire       ts_start_o,    // video_sync ts_start
  output wire       tsr_go_o,      // an object handed to the TS renderer
  output wire       tsr_sprite_o,  // ... it is a sprite (else a tile)
  output wire [8:0] tsr_x_o,
  output wire [7:0] tsr_page_o,
  output wire [3:0] tsr_pal_o,
  output wire       ts_next_o,     // a TS renderer DRAM cycle (c2)
  output wire       tm_next_o,     // a tilemap prefetch DRAM cycle (c2)
  output wire [4:0] curr_cycle_o   // arbiter: owner of the DRAM cycle in progress (0 = free)
);
  import "DPI-C" function int dram_read(input int addr);

  wire f0, f1, h0, h1, c0, c1, c2, c3, ay_clk;

  clock clock
  (
    .clk(clk), .f0(f0), .f1(f1), .h0(h0), .h1(h1),
    .c0(c0), .c1(c1), .c2(c2), .c3(c3), .ay_clk(ay_clk), .ay_mod(2'b00)
  );

  wire [20:0] daddr, video_addr, ts_addr, tm_addr;
  wire dreq, drnw;
  wire [1:0] dbsel;
  wire [15:0] dram_wrdata;
  wire [4:0] video_bw;
  wire video_go, video_strobe, video_pre_next, video_next, next_video;
  wire ts_req, ts_pre_next, ts_next, tm_req, tm_next;
  wire cpu_next, cpu_strobe, cpu_latch, dma_next;
  reg  [15:0] rd = 0;

  arbiter arbiter
  (
    .clk(clk), .c1(c1), .c2(c2), .c3(c3),
    .dram_addr(daddr), .dram_req(dreq), .dram_rnw(drnw), .dram_bsel(dbsel), .dram_wrdata(dram_wrdata),
    .cpu_addr(21'd0), .cpu_wrdata(8'd0), .cpu_req(cpu_req), .cpu_rnw(1'b1), .cpu_wrbsel(1'b0),
    .cpu_next(cpu_next), .cpu_strobe(cpu_strobe), .cpu_latch(cpu_latch),
    .video_go(video_go), .video_bw(video_bw), .video_addr(video_addr),
    .video_strobe(video_strobe), .video_pre_next(video_pre_next), .video_next(video_next), .next_vid(next_video),
    .dma_addr(21'd0), .dma_wrdata(16'd0), .dma_req(1'b0), .dma_rnw(1'b1), .dma_next(dma_next),
    .ts_req(ts_req), .ts_addr(ts_addr), .ts_pre_next(ts_pre_next), .ts_next(ts_next),
    .tm_addr(tm_addr), .tm_req(tm_req), .tm_next(tm_next)
  );

  // Behavioral DRAM: dram.v latches the address at c3; the data of that cycle
  // is valid on the bus until the next c3.
  always @(posedge clk) if (c3 && dreq && drnw)
    rd <= 16'(dram_read({11'd0, daddr}));

  wire [1:0] vred, vgrn, vblu;
  wire [4:0] vred_raw, vgrn_raw, vblu_raw;
  wire vdac_mode, hsync, vsync, csync, int_start_s, line_start_s, frame_start_s;

  video_top video_top
  (
    .clk(clk), .f1(f1), .h1(h1), .c0(c0), .c1(c1), .c3(c3),
    .vred(vred), .vgrn(vgrn), .vblu(vblu),
    .vred_raw(vred_raw), .vgrn_raw(vgrn_raw), .vblu_raw(vblu_raw), .vdac_mode(vdac_mode),
    .hsync(hsync), .vsync(vsync), .csync(csync), .ray_x(ray_x), .ray_y(ray_y),
    .d(8'd0), .zmd(zmd), .zma(zma), .cram_we(1'b0), .sfile_we(sfile_we), .xt_wr_data(xt_wr_data),
    .zborder_wr(1'b0), .border_wr(border_wr), .zvpage_wr(1'b0), .vpage_wr(vpage_wr), .vconf_wr(vconf_wr),
    .gx_offsl_wr(gx_offsl_wr), .gx_offsh_wr(gx_offsh_wr), .gy_offsl_wr(gy_offsl_wr), .gy_offsh_wr(gy_offsh_wr),
    .t0x_offsl_wr(t0x_offsl_wr), .t0x_offsh_wr(t0x_offsh_wr), .t0y_offsl_wr(t0y_offsl_wr), .t0y_offsh_wr(t0y_offsh_wr),
    .t1x_offsl_wr(t1x_offsl_wr), .t1x_offsh_wr(t1x_offsh_wr), .t1y_offsl_wr(t1y_offsl_wr), .t1y_offsh_wr(t1y_offsh_wr),
    .tsconf_wr(tsconf_wr), .palsel_wr(palsel_wr), .tmpage_wr(tmpage_wr), .t0gpage_wr(t0gpage_wr), .t1gpage_wr(t1gpage_wr),
    .sgpage_wr(sgpage_wr), .hint_beg_wr(1'b0), .vint_begl_wr(1'b0), .vint_begh_wr(1'b0),
    .res(res), .int_start_s(int_start_s), .line_start_s(line_start_s), .frame_start_s(frame_start_s),
    .video_addr(video_addr), .video_bw(video_bw), .video_go(video_go), .dram_rdata(rd),
    .video_pre_next(video_pre_next), .video_strobe(video_strobe),
    .ts_addr(ts_addr), .ts_req(ts_req), .ts_pre_next(ts_pre_next), .ts_next(ts_next),
    .tm_addr(tm_addr), .tm_req(tm_req), .tm_next(tm_next),
    .cfg_60hz(1'b0), .vga_on(1'b0)
  );

  assign c0_o = c0;
  assign c1_o = c1;
  assign c2_o = c2;
  assign c3_o = c3;
  assign vdata_o = video_top.video_out.vdata;
  assign vplex_o = video_top.vplex;
  assign hvpix_o = video_top.hvpix;
  assign tv_hires_o = video_top.tv_hires;
  assign line_start_o = line_start_s;
  assign ts_start_o = video_top.ts_start;
  assign tsr_go_o = video_top.tsr_go;
  assign tsr_sprite_o = video_top.video_ts.sprites;
  assign tsr_x_o = video_top.tsr_x;
  assign tsr_page_o = video_top.tsr_page;
  assign tsr_pal_o = video_top.tsr_pal;
  assign ts_next_o = ts_next;
  assign tm_next_o = tm_next;
  assign curr_cycle_o = arbiter.curr_cycle;
endmodule
