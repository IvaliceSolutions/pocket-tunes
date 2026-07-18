// Instruction tightly-coupled memory (ITCM) — 32 KB of 1-cycle BRAM that backs
// the hot Opus decode code (M7d). Code normally lives in the 55 ns external
// SRAM, whose I-cache line refill costs 80 clk_sys cycles (16 halfword reads);
// libopus' large working set thrashes the 4 KB direct-mapped I-cache, so the
// decoder ran below real time. The linker places the hot CELT-decode objects
// in a .itcm section at 0x6000_0000; crt0 copies it here from its SRAM load
// image at boot, and the CPU's iBus refills for region 6 are served from this
// BRAM at ~1 cycle/word instead of 10 — the misses become cheap.
//
// Two ports on one memory (true dual-port M10K):
//   A (dBus, region 6): word writes during the boot copy + 1-cycle reads.
//   B (iBus refill):    read-only 8-word bursts, speaking the sram_ctrl ic_*
//                       protocol so it drops into the existing cache refill mux.

`default_nettype none

module itcm #(
    parameter WORDS = 8192,       // 32 KB
    parameter AW    = 13,         // log2(WORDS)
    parameter LINE_WORDS = 8
) (
    input wire clk,
    input wire reset_n,

    // ---- port A: dBus (boot copy writes + reads), 1-cycle, region-6 addresses
    input  wire        d_sel,     // dbus targets region 6 this cycle
    input  wire        d_wr,
    input  wire [AW+1:0] d_addr,  // byte address (low bits of dbus_addr)
    input  wire [31:0] d_wdata,
    input  wire [ 3:0] d_mask,
    output reg  [31:0] d_rdata,

    // ---- port B: iBus line refill (read-only burst), sram_ctrl ic_* protocol
    input  wire        ic_valid,
    output reg         ic_ready,
    input  wire [31:0] ic_addr,
    output reg         ic_rsp_valid,
    output reg  [31:0] ic_rsp_data
);

  // one memory, byte-lane split so byte-enabled writes infer cleanly
  reg [7:0] m0[0:WORDS-1];
  reg [7:0] m1[0:WORDS-1];
  reg [7:0] m2[0:WORDS-1];
  reg [7:0] m3[0:WORDS-1];

  // ---- port A: dBus (write + registered read)
  wire [AW-1:0] a_word = d_addr[AW+1:2];
  always @(posedge clk) begin
    if (d_sel && d_wr) begin
      if (d_mask[0]) m0[a_word] <= d_wdata[7:0];
      if (d_mask[1]) m1[a_word] <= d_wdata[15:8];
      if (d_mask[2]) m2[a_word] <= d_wdata[23:16];
      if (d_mask[3]) m3[a_word] <= d_wdata[31:24];
    end
    d_rdata <= {m3[a_word], m2[a_word], m1[a_word], m0[a_word]};
  end

  // ---- port B: iBus refill FSM. Only responds to region-6 line addresses.
  reg [AW-1:0] b_word;
  reg [3:0]    cnt;
  reg          busy;
  always @(posedge clk or negedge reset_n) begin
    if (~reset_n) begin
      ic_ready     <= 0;
      ic_rsp_valid <= 0;
      busy         <= 0;
      cnt          <= 0;
    end else begin
      ic_ready     <= 0;
      ic_rsp_valid <= 0;
      if (!busy) begin
        if (ic_valid) begin
          ic_ready <= 1;
          // line-aligned word index within the TCM window
          b_word <= {ic_addr[AW+1:2+3], 3'b000};
          cnt    <= LINE_WORDS[3:0];
          busy   <= 1;
        end
      end else begin
        // stream one word per cycle: registered read is aligned with rsp_valid
        ic_rsp_data  <= {m3[b_word], m2[b_word], m1[b_word], m0[b_word]};
        ic_rsp_valid <= 1;
        b_word <= b_word + 1'b1;
        cnt    <= cnt - 4'd1;
        if (cnt == 4'd1) busy <= 0;
      end
    end
  end

endmodule
