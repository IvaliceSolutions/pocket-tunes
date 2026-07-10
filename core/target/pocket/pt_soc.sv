// Pocket Tunes SoC: PicoRV32 + memories + framebuffer scanout + MMIO
//                   + APF target-command controller + PCM audio FIFO.
//
// Instantiated by core_top for synthesis and by the testbench for simulation,
// so the simulated design is exactly what ships.
//
// CPU memory map (decoded on addr[31:28]):
//   0x0000_0000  CPU RAM, 128 KB, byte-writable, initialized from firmware_b*.hex
//   0x1000_0000  bridge RX RAM, 16 KB, read-only for the CPU (APF writes land
//                here: 0x0180 target-read responses aimed at bridge 0x1000_0000)
//   0x2000_0000  framebuffer 320x288 @ 8bpp (92,160 bytes), write-only
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
    parameter FIRMWARE_B0  = "firmware_b0.hex",
    parameter FIRMWARE_B1  = "firmware_b1.hex",
    parameter FIRMWARE_B2  = "firmware_b2.hex",
    parameter FIRMWARE_B3  = "firmware_b3.hex",
    parameter PALETTE_FILE = "palette.hex"
) (
    input wire clk_sys,
    input wire clk_vid,
    input wire reset_n,  // async-ish (bridge domain); synchronized internally

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
  wire reset_n_vid;
  synch_3 rst_vid_s (reset_n, reset_n_vid, clk_vid);

  // ------------------------------------------------------------------ inputs
  wire [15:0] cont1_key_s;
  synch_3 #(.WIDTH(16)) key_s (cont1_key, cont1_key_s, clk_sys);

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
  // VexRiscv (pipelined rv32im, DSP multiplier, no cache): simple iBus/dBus.
  wire        ibus_cmd_valid, ibus_cmd_ready, ibus_rsp_valid;
  wire [31:0] ibus_pc, ibus_inst;

  wire        dbus_cmd_valid, dbus_cmd_ready, dbus_wr;
  wire [ 3:0] dbus_mask;
  wire [31:0] dbus_addr, dbus_wdata, dbus_rdata;
  wire [ 1:0] dbus_size;
  wire        dbus_rsp_valid;

  VexRiscv cpu (
      .clk  (clk_sys),
      .reset(~reset_n_sys),  // active high

      .timerInterrupt   (1'b0),
      .externalInterrupt(1'b0),
      .softwareInterrupt(1'b0),

      .iBus_cmd_valid      (ibus_cmd_valid),
      .iBus_cmd_ready      (ibus_cmd_ready),
      .iBus_cmd_payload_pc (ibus_pc),
      .iBus_rsp_valid      (ibus_rsp_valid),
      .iBus_rsp_payload_error(1'b0),
      .iBus_rsp_payload_inst (ibus_inst),

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

  // ---- iBus: always-ready single fetch from CPU RAM (port A), 1-cycle rsp
  assign ibus_cmd_ready = 1'b1;
  reg ibus_rsp_valid_r;
  always @(posedge clk_sys) ibus_rsp_valid_r <= ibus_cmd_valid & reset_n_sys;
  assign ibus_rsp_valid = ibus_rsp_valid_r;

  // ---- dBus: region-decoded, 1-cycle rsp for every access
  wire [3:0] dregion = dbus_addr[31:28];
  assign dbus_cmd_ready = 1'b1;
  wire d_acc   = dbus_cmd_valid & reset_n_sys;
  wire dwrite  = d_acc & dbus_wr;
  wire mmio_wr = dwrite & (dregion == 4'hF);
  wire param_sel = dbus_addr[12];  // param struct RAM at 0xF000_1xxx

  reg dbus_rsp_valid_r;
  reg [3:0] dregion_q;
  always @(posedge clk_sys) begin
    dbus_rsp_valid_r <= d_acc;
    dregion_q <= dregion;
  end
  assign dbus_rsp_valid = dbus_rsp_valid_r;

  // ------------------------------------------------------------- memories
  wire [31:0] ram_b_rdata;
  cpu_ram_dp #(
      .WORDS(32768),
      .INIT_B0(FIRMWARE_B0),
      .INIT_B1(FIRMWARE_B1),
      .INIT_B2(FIRMWARE_B2),
      .INIT_B3(FIRMWARE_B3)
  ) ram (
      .clk    (clk_sys),
      .a_word (ibus_pc[16:2]),
      .a_we   (1'b0),          // iBus is read-only; port kept for symmetric TDP
      .a_wdata(32'b0),
      .a_rdata(ibus_inst),
      .b_word (dbus_addr[16:2]),
      .b_we   ((dregion == 4'h0) ? {4{dwrite}} & dbus_mask : 4'h0),
      .b_wdata(dbus_wdata),
      .b_rdata(ram_b_rdata)
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
    if (bridge_rd) param_q_74 <= param_ram[bridge_addr[8:2]];
  end
  assign param_rd_data = param_q_74;

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
      8'h20:   mmio_rdata <= {16'd0, tgt_id};
      8'h24:   mmio_rdata <= tgt_offset;
      8'h28:   mmio_rdata <= tgt_bridgeaddr;
      8'h2C:   mmio_rdata <= tgt_length;
      8'h30:   mmio_rdata <= {30'd0, tgt_cmd};
      8'h34:   mmio_rdata <= {25'd0, tgt_err_s, 2'b00, tgt_done_s, tgt_ack_s};
      8'h40:   mmio_rdata <= {20'd0, pcm_free};
      8'h54:   mmio_rdata <= dt_q_s;
      default: mmio_rdata <= 32'd0;
    endcase
  end

  assign dbus_rdata = (dregion_q == 4'h0) ? ram_b_rdata
                    : (dregion_q == 4'h1) ? rx_rdata
                    : (dregion_q == 4'h2) ? 32'd0
                    : mmio_rdata;

endmodule
