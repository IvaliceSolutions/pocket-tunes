// Pocket Tunes SoC: VexRiscv (rv32im, 4 KB I-cache) + memories + framebuffer
//                   scanout + MMIO + APF target-command controller
//                   + PCM audio FIFO + 16 KB instruction TCM.
//
// Instantiated by core_top for synthesis and by the testbench for simulation,
// so the simulated design is exactly what ships.
//
// CPU memory map (decoded on addr[31:28]):
//   0x0000_0000  CPU RAM, 128 KB, byte-writable, initialized from firmware_b*.hex
//   0x1000_0000  bridge RX RAM, 16 KB, read-only for the CPU (APF writes land
//                here: 0x0180 target-read responses aimed at bridge 0x1000_0000)
//   0x2000_0000  framebuffer 320x288 @ 8bpp (92,160 bytes), write-only
//   0x5000_0000  external async SRAM, 256 KB: firmware code (iBus, cached)
//   0x6000_0000  instruction TCM, 16 KB BRAM: hot Opus decode code (M7d) —
//                iBus refills at 1 cycle/word instead of the SRAM's ~10;
//                dBus writes here only during the crt0 boot copy
//   0xF000_0000  MMIO:
//     +0x00  r   cont1_key (buttons, synchronized)
//     +0x04  r   frame counter (increments each vblank)
//     +0x0C  r   vblank flag
//     +0x10  r   free-running cycle counter (clk_sys)
//     +0x20  rw  target: data slot id
//     +0x24  rw  target: slot offset (bytes)
//     +0x28  rw  target: bridge address (use 0x1000_0000 → RX RAM)
//     +0x2C  rw  target: length (bytes)
//     +0x30  rw  target: command level — 0 idle / 1 read / 2 openfile / 3 getfile
//                (write params first; 0→N edge fires; hold until done, write 0)
//     +0x34  r   target: {err[6:4], done[1], ack[0]}
//     +0x40  w   PCM push {right[31:16], left[15:0]} signed
//     +0x40  r   PCM fifo free space (samples)
//     +0x50  w   datatable word address (slot i: word 2i = id, 2i+1 = size)
//     +0x54  r   datatable word (wait ≥16 cycles after writing +0x50)
//     +0x100..0x2FC  w  param/response struct RAM (128 words):
//                bytes 0-255 = null-terminated path, +0x100 = flags,
//                +0x104 = size — served to APF via bridge reads at 0x4000_0000

`default_nettype none

module pt_soc #(
    parameter PALETTE_FILE = "palette.hex"
) (
    input wire clk_sys,
    input wire clk_vid,
    input wire reset_n,  // async-ish (bridge domain); synchronized internally
    // Memory-subsystem reset: PLL lock, NOT the APF core reset. The APF pushes
    // the Firmware slot into SRAM WHILE the core is held in reset (log: the
    // 0x5000_0000 load lands well before "Reset Exit"), so anything gated by
    // reset_n would drop every loader write and the CPU would boot into an
    // empty SRAM. Same reason bridge_rx_ram's writes carry no reset at all.
    input wire pll_locked,

    // APF bridge (clk_74a domain)
    input  wire        clk_74a,
    input  wire        bridge_wr,
    input  wire        bridge_rd,
    input  wire        bridge_endian_little,
    input  wire [31:0] bridge_addr,
    input  wire [31:0] bridge_wr_data,
    output wire [31:0] param_rd_data,  // bridge reads at 0x4xxxxxxx (param struct)

    // APF target-command interface (wire to core_bridge_cmd; clk_74a domain)
    output wire        target_dataslot_read,
    output wire        target_dataslot_openfile,
    output wire        target_dataslot_getfile,
    input  wire        target_dataslot_ack,
    input  wire        target_dataslot_done,
    input  wire [ 2:0] target_dataslot_err,
    output wire [15:0] target_dataslot_id,
    output wire [31:0] target_dataslot_slotoffset,
    output wire [31:0] target_dataslot_bridgeaddr,
    output wire [31:0] target_dataslot_length,
    output wire [31:0] target_buffer_param_struct,
    output wire [31:0] target_buffer_resp_struct,

    // datatable window (clk_74a domain BRAM inside core_bridge_cmd)
    output wire [ 9:0] datatable_addr,
    input  wire [31:0] datatable_q,

    // controller (raw; synchronized internally)
    input wire [15:0] cont1_key,

    // real-time clock (BCD 0x00HHMMSS, clk_74a domain; quasi-static — changes
    // once a second — so a plain synchronizer is enough)
    input wire [31:0] rtc_time_bcd,

    // M7a: firmware code lives in the external async SRAM
    output wire [16:0] sram_a,
    inout  wire [15:0] sram_dq,
    output wire        sram_oe_n,
    output wire        sram_we_n,
    output wire        sram_ub_n,
    output wire        sram_lb_n,
    // CPU released only after the APF loaded the Firmware slot into SRAM
    input  wire        cpu_run,

    // savestate/sleep handshake (levels; reqs are clk_74a, dones are clk_sys)
    input  wire ss_save_req,
    input  wire ss_load_req,
    output wire ss_save_done,
    output wire ss_load_done,

    // audio samples out (clk_sys domain; updated at 48 kHz)
    output reg signed [15:0] audio_l,
    output reg signed [15:0] audio_r,
    output reg               audio_stb,  // 1-cycle pulse per 48 kHz sample (push strobe for i2s)

    // video out (registered, aligned)
    output wire        video_de,
    output wire        video_hs,
    output wire        video_vs,
    output wire [23:0] video_rgb
);

  // ------------------------------------------------------------------ resets
  wire reset_n_sys;
  synch_3 rst_sys_s (reset_n, reset_n_sys, clk_sys);
  wire sram_rst_n;  // memory subsystem: alive as soon as the clock is stable
  synch_3 rst_sram_s (pll_locked, sram_rst_n, clk_sys);
  // hold the CPU until the Firmware data slot finished loading into SRAM
  wire cpu_run_s;
  synch_3 cpurun_s (cpu_run, cpu_run_s, clk_sys);
  wire cpu_reset_n = reset_n_sys & cpu_run_s;
  wire reset_n_vid;
  synch_3 rst_vid_s (reset_n, reset_n_vid, clk_vid);

  // ------------------------------------------------------------------ inputs
  wire [15:0] cont1_key_s;
  synch_3 #(.WIDTH(16)) key_s (cont1_key, cont1_key_s, clk_sys);
  wire [31:0] rtc_time_s;
  synch_3 #(.WIDTH(32)) rtc_s (rtc_time_bcd, rtc_time_s, clk_sys);

  // ------------------------------------------------------------------ video
  wire de, hs, vs, de_falling, frame_start;
  wire [9:0] vx, vy, vnx, vny;

  video_gen #(
      .H_ACTIVE(320),
      .H_FP    (40),
      .H_SYNC  (32),
      .H_BP    (108),  // total 500
      .V_ACTIVE(288),
      .V_FP    (30),
      .V_SYNC  (3),
      .V_BP    (79)    // total 400 → 500*400 @ 12 MHz = exact 60 Hz
  ) vg (
      .clk        (clk_vid),
      .reset_n    (reset_n_vid),
      .de         (de),
      .hs         (hs),
      .vs         (vs),
      .de_falling (de_falling),
      .x          (vx),
      .y          (vy),
      .next_x     (vnx),
      .next_y     (vny),
      .frame_start(frame_start)
  );

  wire [14:0] fb_word_addr;
  wire [31:0] fb_b_rdata;

  fb_scanout #(
      .PALETTE_FILE(PALETTE_FILE)
  ) scanout (
      .clk_vid(clk_vid),
      .de(de),
      .hs(hs),
      .vs(vs),
      .x(vx),
      .y(vy),
      .next_x(vnx),
      .next_y(vny),
      .fb_word_addr(fb_word_addr),
      .fb_rdata(fb_b_rdata),
      .de_out(video_de),
      .hs_out(video_hs),
      .vs_out(video_vs),
      .rgb_out(video_rgb)
  );

  // vblank → clk_sys (flag + frame counter for the CPU)
  wire in_vblank_vid = (vy >= 10'd288);
  wire in_vblank_s;
  synch_3 vbl_s (in_vblank_vid, in_vblank_s, clk_sys);

  reg [31:0] frame_count = 0;
  reg vbl_prev = 0;
  always @(posedge clk_sys) begin
    vbl_prev <= in_vblank_s;
    if (in_vblank_s && !vbl_prev) frame_count <= frame_count + 1;
  end

  reg [31:0] cycles = 0;
  always @(posedge clk_sys) cycles <= cycles + 1;

  // ------------------------------------------------------------------- CPU
  // VexRiscv v2 (M7a): 4 KB I-cache fetching code from the external SRAM
  // (reset vector 0x5000_0000); dBus stays simple/uncached so all hot data
  // keeps its 1-cycle BRAM path.
  wire        ibus_cmd_valid, ibus_cmd_ready, ibus_rsp_valid;
  wire [31:0] ibus_addr, ibus_rsp_data;
  wire [ 2:0] ibus_size;

  wire        dbus_cmd_valid, dbus_cmd_ready, dbus_wr;
  wire [ 3:0] dbus_mask;
  wire [31:0] dbus_addr, dbus_wdata, dbus_rdata;
  wire [ 1:0] dbus_size;
  wire        dbus_rsp_valid;

  VexRiscv cpu (
      .clk  (clk_sys),
      .reset(~cpu_reset_n),  // active high; held until SRAM is loaded

      .timerInterrupt   (1'b0),
      .externalInterrupt(1'b0),
      .softwareInterrupt(1'b0),

      .iBus_cmd_valid          (ibus_cmd_valid),
      .iBus_cmd_ready          (ibus_cmd_ready),
      .iBus_cmd_payload_address(ibus_addr),
      .iBus_cmd_payload_size   (ibus_size),
      .iBus_rsp_valid          (ibus_rsp_valid),
      .iBus_rsp_payload_data   (ibus_rsp_data),
      .iBus_rsp_payload_error  (1'b0),

      .dBus_cmd_valid         (dbus_cmd_valid),
      .dBus_cmd_ready         (dbus_cmd_ready),
      .dBus_cmd_payload_wr     (dbus_wr),
      .dBus_cmd_payload_mask   (dbus_mask),
      .dBus_cmd_payload_address(dbus_addr),
      .dBus_cmd_payload_data   (dbus_wdata),
      .dBus_cmd_payload_size   (dbus_size),
      .dBus_rsp_ready(dbus_rsp_valid),  // "ready" here = response valid
      .dBus_rsp_error(1'b0),
      .dBus_rsp_data (dbus_rdata)
  );

  // ---- dBus: region-decoded. BRAM/RX/FB/MMIO answer in 1 cycle as before;
  // region 5 (SRAM: Opus tables, boot-time copies) takes multiple cycles
  // through the SRAM arbiter, stalling via cmd_ready.
  wire [3:0] dregion = dbus_addr[31:28];
  wire d_sram_sel = (dregion == 4'h5);
  wire d_sram_accept, d_sram_done;
  wire [31:0] d_sram_rdata;
  reg  d_sram_inflight;
  always @(posedge clk_sys) begin
    if (~reset_n_sys) d_sram_inflight <= 0;
    else if (d_sram_accept) d_sram_inflight <= 1;
    else if (d_sram_done) d_sram_inflight <= 0;
  end

  assign dbus_cmd_ready = d_sram_sel ? d_sram_accept : 1'b1;
  wire d_acc   = dbus_cmd_valid & reset_n_sys & ~d_sram_sel;
  wire dwrite  = d_acc & dbus_wr;
  wire mmio_wr = dwrite & (dregion == 4'hF);
  wire param_sel = dbus_addr[12];  // param struct RAM at 0xF000_1xxx

  reg dbus_rsp_valid_r;
  reg [3:0] dregion_q;
  reg d_sram_rsp_q;
  always @(posedge clk_sys) begin
    dbus_rsp_valid_r <= d_acc;
    dregion_q <= dregion;
    d_sram_rsp_q <= d_sram_done;
  end
  reg [31:0] d_sram_rdata_q;
  always @(posedge clk_sys) if (d_sram_done) d_sram_rdata_q <= d_sram_rdata;
  assign dbus_rsp_valid = dbus_rsp_valid_r | d_sram_rsp_q;

  // ------------------------------------------------------------- memories
  // Data-only BRAM (128 KB): code moved to SRAM, so the dual-port trick and
  // the firmware init hexes are gone. crt0 fills .rodata/.data at boot.
  wire [31:0] ram_b_rdata;
  cpu_ram_sp #(
      .WORDS(32768)
  ) ram (
      .clk  (clk_sys),
      .word (dbus_addr[16:2]),
      .we   ((dregion == 4'h0) ? {4{dwrite}} & dbus_mask : 4'h0),
      .wdata(dbus_wdata),
      .rdata(ram_b_rdata)
  );

  // ---------------------------------------------- external SRAM (firmware)
  // Loader: the APF writes the Firmware slot to bridge 0x5xxx_xxxx.
  wire        sram_ld_en;
  wire [27:0] sram_ld_addr;
  wire [15:0] sram_ld_data;
  data_loader #(
      .ADDRESS_MASK_UPPER_4     (4'h5),
      .ADDRESS_SIZE             (28),
      .WRITE_MEM_CLOCK_DELAY    (8),
      .WRITE_MEM_EN_CYCLE_LENGTH(1),
      .OUTPUT_WORD_SIZE         (2)
  ) fw_loader (
      .clk_74a   (clk_74a),
      .clk_memory(clk_sys),

      .bridge_wr           (bridge_wr),
      .bridge_endian_little(bridge_endian_little),
      .bridge_addr         (bridge_addr),
      .bridge_wr_data      (bridge_wr_data),

      .write_en  (sram_ld_en),
      .write_addr(sram_ld_addr),
      .write_data(sram_ld_data)
  );

  // I-cache refill mux wires (driven below): hot Opus code (region 6) is
  // served from the 1-cycle ITCM BRAM, everything else from the SRAM.
  wire        ic_itcm_sel = (ibus_addr[31:28] == 4'h6);
  wire        sram_ic_valid = ibus_cmd_valid & ~ic_itcm_sel;
  wire        itcm_ic_valid = ibus_cmd_valid &  ic_itcm_sel;
  wire        sram_ic_ready, itcm_ic_ready;
  wire        sram_ic_rsp_valid, itcm_ic_rsp_valid;
  wire [31:0] sram_ic_rsp_data, itcm_ic_rsp_data;

  sram_ctrl sramc (
      .clk    (clk_sys),
      .reset_n(sram_rst_n),  // NOT the core reset — see pll_locked above

      .sram_a   (sram_a),
      .sram_dq  (sram_dq),
      .sram_oe_n(sram_oe_n),
      .sram_we_n(sram_we_n),
      .sram_ub_n(sram_ub_n),
      .sram_lb_n(sram_lb_n),

      .ld_valid(sram_ld_en),
      .ld_addr ({4'h0, sram_ld_addr}),
      .ld_data (sram_ld_data),

      .d_valid (dbus_cmd_valid & d_sram_sel & ~d_sram_inflight),
      .d_accept(d_sram_accept),
      .d_done  (d_sram_done),
      .d_wr    (dbus_wr),
      .d_addr  (dbus_addr),
      .d_wdata (dbus_wdata),
      .d_mask  (dbus_mask),
      .d_rdata (d_sram_rdata),

      .ic_valid    (sram_ic_valid),
      .ic_ready    (sram_ic_ready),
      .ic_addr     (ibus_addr),
      .ic_rsp_valid(sram_ic_rsp_valid),
      .ic_rsp_data (sram_ic_rsp_data)
  );

  // ---- I-cache refill mux: only one refill is in flight at a time, so the
  // response is muxed by whichever side is currently pulsing rsp_valid.
  assign ibus_cmd_ready = ic_itcm_sel ? itcm_ic_ready : sram_ic_ready;
  assign ibus_rsp_valid = sram_ic_rsp_valid | itcm_ic_rsp_valid;
  assign ibus_rsp_data  = itcm_ic_rsp_valid ? itcm_ic_rsp_data : sram_ic_rsp_data;

  // ITCM (region 6): boot-copied hot decode code, iBus 1-cycle refills + a
  // dBus write port for the crt0 copy.
  wire        d_itcm_sel = (dregion == 4'h6);
  wire [31:0] itcm_rdata;
  itcm #(.WORDS(4096), .AW(12)) itcm_i (
      .clk    (clk_sys),
      .reset_n(sram_rst_n),   // alive with the clock, like the SRAM subsystem
      .d_sel  (d_acc & d_itcm_sel),
      .d_wr   (dbus_wr),
      .d_addr (dbus_addr[13:0]),
      .d_wdata(dbus_wdata),
      .d_mask (dbus_mask),
      .d_rdata(itcm_rdata),
      .ic_valid    (itcm_ic_valid),
      .ic_ready    (itcm_ic_ready),
      .ic_addr     (ibus_addr),
      .ic_rsp_valid(itcm_ic_rsp_valid),
      .ic_rsp_data (itcm_ic_rsp_data)
  );

  wire [31:0] rx_rdata;
  bridge_rx_ram #(
      .MASK_UPPER_4(4'h1),
      .WORDS(4096)
  ) rx_ram (
      .clk_74a(clk_74a),
      .clk_sys(clk_sys),

      .bridge_wr           (bridge_wr),
      .bridge_endian_little(bridge_endian_little),
      .bridge_addr         (bridge_addr),
      .bridge_wr_data      (bridge_wr_data),

      .rd_word_addr(dbus_addr[13:2]),
      .rd_data     (rx_rdata)
  );

  pt_framebuffer fb (
      .a_clk  (clk_sys),
      .a_sel  (dwrite && dregion == 4'h2),
      .a_wstrb(dbus_mask),
      .a_addr (dbus_addr),
      .a_wdata(dbus_wdata),

      .b_clk      (clk_vid),
      .b_word_addr(fb_word_addr),
      .b_rdata    (fb_b_rdata)
  );

  // -------------------------------------------------- target-command control
  reg [15:0] tgt_id = 0;
  reg [31:0] tgt_offset = 0;
  reg [31:0] tgt_bridgeaddr = 32'h1000_0000;
  reg [31:0] tgt_length = 0;
  reg [ 1:0] tgt_cmd = 0;  // 0 idle / 1 read / 2 openfile / 3 getfile

  assign target_dataslot_id         = tgt_id;
  assign target_dataslot_slotoffset = tgt_offset;
  assign target_dataslot_bridgeaddr = tgt_bridgeaddr;
  assign target_dataslot_length     = tgt_length;
  assign target_buffer_param_struct = 32'h4000_0000;
  assign target_buffer_resp_struct  = 32'h1000_0000;  // getfile answer → RX RAM

  // command level → clk_74a (edge-detected there); params are stable-before-edge
  wire [1:0] tgt_cmd_74;
  synch_3 #(.WIDTH(2)) cmd_s (tgt_cmd, tgt_cmd_74, clk_74a);
  assign target_dataslot_read     = (tgt_cmd_74 == 2'd1);
  assign target_dataslot_openfile = (tgt_cmd_74 == 2'd2);
  assign target_dataslot_getfile  = (tgt_cmd_74 == 2'd3);

  // status → clk_sys
  wire tgt_ack_s, tgt_done_s;
  wire [2:0] tgt_err_s;
  synch_3 ack_s (target_dataslot_ack, tgt_ack_s, clk_sys);
  synch_3 done_s (target_dataslot_done, tgt_done_s, clk_sys);
  synch_3 #(.WIDTH(3)) err_s (target_dataslot_err, tgt_err_s, clk_sys);

  // param/response struct RAM: 128 words. CPU writes (MMIO +0x100..0x2FC),
  // bridge reads continuously at 0x4xxxxxxx (registered, stable before use)
  reg [31:0] param_ram[0:127];
  reg [31:0] param_q_74;
  always @(posedge clk_sys) begin
    if (mmio_wr && param_sel) param_ram[dbus_addr[8:2]] <= dbus_wdata;
  end
  always @(posedge clk_74a) begin
    if (bridge_rd)
      param_q_74 <= param_ram[bridge_addr[12] ? (7'd96 + {3'd0, bridge_addr[5:2]})
                                              : bridge_addr[8:2]];
  end
  assign param_rd_data = param_q_74;

  // -------------------------------------------------- savestate (sleep/wake)
  // Wake: the host writes the 64-byte state blob to bridge 0x4000_1000..103F;
  // capture it into registers (quasi-static: written before ss_load fires,
  // read by the CPU only after the synchronized load_req is seen).
  reg [31:0] ss_load_word[0:15];
  always @(posedge clk_74a) begin
    if (bridge_wr && bridge_addr[31:28] == 4'h4 && bridge_addr[12]
        && bridge_addr[11:6] == 6'd0)
      ss_load_word[bridge_addr[5:2]] <= bridge_wr_data;
  end

  // req levels → clk_sys for the firmware; done levels → back to clk_74a
  wire ss_save_req_s, ss_load_req_s;
  synch_3 ss_sv_s (ss_save_req, ss_save_req_s, clk_sys);
  synch_3 ss_ld_s (ss_load_req, ss_load_req_s, clk_sys);
  reg [1:0] ss_done;  // bit0 save_done, bit1 load_done (firmware-owned levels)
  assign ss_save_done = ss_done[0];
  assign ss_load_done = ss_done[1];

  // datatable window
  reg [9:0] dt_addr = 0;
  assign datatable_addr = dt_addr;
  wire [31:0] dt_q_s;
  synch_3 #(.WIDTH(32)) dtq_s (datatable_q, dt_q_s, clk_sys);

  // ------------------------------------------------------------ PCM FIFO
  // 4096 stereo samples (~85 ms) — deep enough to ride out a full-screen UI
  // redraw without starving. CPU pushes; a 48 kHz strobe (72 MHz / 1500) pops.
  reg [31:0] pcm_mem[0:4095];
  reg [11:0] pcm_wr = 0, pcm_rd = 0;
  wire [11:0] pcm_level = pcm_wr - pcm_rd;
  wire [11:0] pcm_free = 12'd4095 - pcm_level;

  reg [10:0] pcm_div = 0;
  wire pcm_tick = (pcm_div == 11'd1499);  // 72MHz/1500 = 48kHz

  reg [31:0] pcm_q;

  always @(posedge clk_sys) begin
    if (!reset_n_sys) begin
      pcm_wr <= 0;
      pcm_rd <= 0;
      pcm_div <= 0;
      audio_l <= 0;
      audio_r <= 0;
      audio_stb <= 0;
    end else begin
      if (mmio_wr && dbus_addr[9:0] == 10'h040 && pcm_free != 0) begin
        pcm_mem[pcm_wr[11:0]] <= dbus_wdata;
        pcm_wr <= pcm_wr + 1'b1;
      end

      pcm_div <= pcm_tick ? 11'd0 : pcm_div + 1'b1;
      pcm_q <= pcm_mem[pcm_rd[11:0]];
      // Strobe EVERY 48 kHz tick so sound_i2s pushes one sample per period —
      // its own change-detect drops identical consecutive samples (common in
      // speech/silence → crackle). On underflow we re-push the held sample,
      // which keeps the producer/consumer sample rates locked.
      audio_stb <= pcm_tick;
      if (pcm_tick && pcm_level != 0) begin
        audio_l <= pcm_q[15:0];
        audio_r <= pcm_q[31:16];
        pcm_rd  <= pcm_rd + 1'b1;
      end
      // underflow: hold last sample
    end
  end

  // ---------------------------------------------------------------- MMIO
  always @(posedge clk_sys) begin
    if (mmio_wr && !param_sel) begin
      case (dbus_addr[7:0])
        8'h20: tgt_id <= dbus_wdata[15:0];
        8'h24: tgt_offset <= dbus_wdata;
        8'h28: tgt_bridgeaddr <= dbus_wdata;
        8'h2C: tgt_length <= dbus_wdata;
        8'h30: tgt_cmd <= dbus_wdata[1:0];
        8'h50: dt_addr <= dbus_wdata[9:0];
        8'hA4: ss_done <= dbus_wdata[1:0];
        default: ;
      endcase
    end
  end

  reg [31:0] mmio_rdata;
  always @(posedge clk_sys) begin
    case (dbus_addr[7:0])
      8'h00:   mmio_rdata <= {16'd0, cont1_key_s};
      8'h04:   mmio_rdata <= frame_count;
      8'h0C:   mmio_rdata <= {31'd0, in_vblank_s};
      8'h10:   mmio_rdata <= cycles;
      8'h14:   mmio_rdata <= rtc_time_s;  // BCD 0x00HHMMSS
      8'h20:   mmio_rdata <= {16'd0, tgt_id};
      8'h24:   mmio_rdata <= tgt_offset;
      8'h28:   mmio_rdata <= tgt_bridgeaddr;
      8'h2C:   mmio_rdata <= tgt_length;
      8'h30:   mmio_rdata <= {30'd0, tgt_cmd};
      8'h34:   mmio_rdata <= {25'd0, tgt_err_s, 2'b00, tgt_done_s, tgt_ack_s};
      8'h40:   mmio_rdata <= {20'd0, pcm_free};
      8'h54:   mmio_rdata <= dt_q_s;
      8'hA0:   mmio_rdata <= {30'd0, ss_load_req_s, ss_save_req_s};
      default: mmio_rdata <= (dbus_addr[7:6] == 2'b11) ? ss_load_word[dbus_addr[5:2]]
                                                       : 32'd0;
    endcase
  end

  assign dbus_rdata = d_sram_rsp_q ? d_sram_rdata_q
                    : (dregion_q == 4'h0) ? ram_b_rdata
                    : (dregion_q == 4'h6) ? itcm_rdata
                    : (dregion_q == 4'h1) ? rx_rdata
                    : (dregion_q == 4'h2) ? 32'd0
                    : mmio_rdata;

endmodule
