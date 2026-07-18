// Async-SRAM controller + 3-client arbiter (single clock, clk_sys).
//
// The Pocket's 256 KB async SRAM (AS6C2016-55: 128K x 16, 55 ns!) holds the
// firmware code (+ the Opus tables): written once at boot by the APF loader,
// then serves instruction-cache line refills and occasional uncached dBus
// reads. The part is SLOW:every 16-bit op takes ACCESS_CYCLES clocks with the
// address held stable — 5 cycles @72 MHz = 69 ns > 55 ns + IO margins.
//
// Clients (fixed priority, all in clk domain):
//   1. loader  — 16-bit writes from the APF data_loader (boot only, CPU held
//                in reset; pacing is one halfword per ~30+ cycles, no skid
//                buffer needed beyond a 1-deep capture)
//   2. dbus    — one 32-bit read or write (two 16-bit ops, little-endian:
//                low halfword at addr, high at addr+2)
//   3. icache  — line refill burst: LINE_WORDS sequential 32-bit reads
//
// dq tristate is driven only during the two cycles of a write op.

`default_nettype none

module sram_ctrl #(
    parameter LINE_WORDS = 8,     // icache line = 32 B
    parameter ACCESS_CYCLES = 5   // clocks per 16-bit op (55 ns part @ 72 MHz)
) (
    input wire clk,
    input wire reset_n,

    // physical async SRAM
    output reg  [16:0] sram_a,
    inout  wire [15:0] sram_dq,
    output reg         sram_oe_n,
    output reg         sram_we_n,
    output reg         sram_ub_n,
    output reg         sram_lb_n,

    // loader (16-bit write PULSES, byte address, halfword aligned; buffered
    // 2-deep because a pulse can land while an op is in flight)
    input wire        ld_valid,
    input wire [31:0] ld_addr,
    input wire [15:0] ld_data,

    // dBus (32-bit, single op; addr is byte address, word aligned)
    input  wire        d_valid,
    output reg         d_accept, // 1-cycle pulse: request latched (consume cmd)
    output reg         d_done,   // 1-cycle pulse: rdata valid / write finished
    input  wire        d_wr,
    input  wire [31:0] d_addr,
    input  wire [31:0] d_wdata,
    input  wire [ 3:0] d_mask,
    output reg  [31:0] d_rdata,

    // icache refill (burst of LINE_WORDS words from a line-aligned address)
    input  wire        ic_valid,
    output reg         ic_ready,   // accepted (1 cycle)
    input  wire [31:0] ic_addr,
    output reg         ic_rsp_valid,
    output reg  [31:0] ic_rsp_data
);

  // write path drives dq for both cycles of a write op
  reg        dq_drive;
  reg [15:0] dq_out;
  assign sram_dq = dq_drive ? dq_out : 16'hZZZZ;

  localparam S_IDLE = 3'd0, S_SETUP = 3'd1, S_SAMPLE = 3'd2;
  reg [2:0] state = S_IDLE;
  reg [2:0] waitc;

  localparam CL_LD = 2'd0, CL_D = 2'd1, CL_IC = 2'd2;
  reg [1:0] client;
  reg       half;        // which halfword of the current 32-bit op (0 = low)
  reg [3:0] burst_cnt;   // icache words remaining
  reg [16:0] cur_a;      // halfword address
  reg        cur_wr;
  reg [15:0] cur_wdata;
  reg [ 1:0] cur_be;     // byte enables for the current halfword
  reg [15:0] lo_hold;    // low halfword of a 32-bit read

  // 2-deep loader skid buffer (pulses arrive every ~8 cycles; ops take ~6)
  reg [48:0] ld_q[0:1];   // {addr[31:0], data[15:0], valid}
  reg        ld_wp, ld_rp;
  wire ld_pend = ld_q[ld_rp][0];

  // latched dbus request (so d_* may change after d_done)
  reg        dq_wr;
  reg [31:0] dq_addr, dq_wdata;
  reg [ 3:0] dq_mask;

  always @(posedge clk or negedge reset_n) begin
    if (~reset_n) begin
      state <= S_IDLE;
      sram_oe_n <= 1;
      sram_we_n <= 1;
      sram_ub_n <= 1;
      sram_lb_n <= 1;
      dq_drive <= 0;
      d_done <= 0;
      d_accept <= 0;
      ic_ready <= 0;
      ic_rsp_valid <= 0;
      waitc <= 0;
      ld_wp <= 0;
      ld_rp <= 0;
      ld_q[0][0] <= 0;
      ld_q[1][0] <= 0;
    end else begin
      d_done <= 0;
      d_accept <= 0;
      ic_ready <= 0;
      ic_rsp_valid <= 0;

      // capture loader pulses regardless of FSM state
      if (ld_valid) begin
        ld_q[ld_wp] <= {ld_addr, ld_data, 1'b1};
        ld_wp <= ~ld_wp;
      end

      case (state)
        S_IDLE: begin
          sram_we_n <= 1;
          sram_oe_n <= 1;
          dq_drive  <= 0;
          if (ld_pend) begin
            client <= CL_LD;
            cur_a <= ld_q[ld_rp][34:18];
            cur_wr <= 1;
            cur_wdata <= ld_q[ld_rp][16:1];
            cur_be <= 2'b11;
            half <= 0;
            burst_cnt <= 0;
            ld_q[ld_rp][0] <= 1'b0;
            ld_rp <= ~ld_rp;
            state <= S_SETUP;
          end else if (d_valid && !d_done) begin
            client <= CL_D;
            d_accept <= 1;
            dq_wr <= d_wr;
            dq_addr <= d_addr;
            dq_wdata <= d_wdata;
            dq_mask <= d_mask;
            cur_a <= d_addr[17:1] & 17'h1FFFE;  // low halfword of the word
            cur_wr <= d_wr;
            cur_wdata <= d_wdata[15:0];
            cur_be <= d_mask[1:0];
            half <= 0;
            burst_cnt <= 0;
            state <= S_SETUP;
          end else if (ic_valid) begin
            client <= CL_IC;
            ic_ready <= 1;
            cur_a <= ic_addr[17:1] & ~17'h0;    // line-aligned by the cache
            cur_wr <= 0;
            cur_be <= 2'b11;
            half <= 0;
            burst_cnt <= LINE_WORDS[3:0];
            state <= S_SETUP;
          end
        end

        S_SETUP: begin
          // present address (and data/WE for writes) and hold ACCESS_CYCLES-1
          // clocks so the 55 ns part settles before we sample / end the write
          sram_a <= cur_a;
          sram_ub_n <= cur_wr ? ~cur_be[1] : 1'b0;
          sram_lb_n <= cur_wr ? ~cur_be[0] : 1'b0;
          if (cur_wr && cur_be != 2'b00) begin
            dq_drive <= 1;
            dq_out <= cur_wdata;
            sram_we_n <= 0;
            sram_oe_n <= 1;
          end else begin
            dq_drive <= 0;
            sram_we_n <= 1;
            sram_oe_n <= 1'b0;
          end
          if (waitc == ACCESS_CYCLES[2:0] - 3'd2) begin
            waitc <= 0;
            state <= S_SAMPLE;
          end else begin
            waitc <= waitc + 3'd1;
          end
        end

        S_SAMPLE: begin
          // end of the 2-cycle op: sample reads, deassert write strobe
          sram_we_n <= 1;
          if (!cur_wr) begin
            if (half == 0) lo_hold <= sram_dq;
          end

          if (client == CL_LD) begin
            state <= S_IDLE;
          end else if (half == 0) begin
            // advance to the high halfword of this 32-bit word
            half <= 1;
            cur_a <= cur_a + 17'd1;
            if (client == CL_D) begin
              cur_wdata <= dq_wdata[31:16];
              cur_be <= dq_mask[3:2];
            end
            state <= S_SETUP;
          end else begin
            // 32-bit op complete
            if (client == CL_D) begin
              if (!cur_wr) d_rdata <= {sram_dq, lo_hold};
              d_done <= 1;
              state <= S_IDLE;
            end else begin  // icache burst word
              ic_rsp_valid <= 1;
              ic_rsp_data <= {sram_dq, lo_hold};
              half <= 0;
              cur_a <= cur_a + 17'd1;
              burst_cnt <= burst_cnt - 4'd1;
              state <= (burst_cnt == 4'd1) ? S_IDLE : S_SETUP;
            end
          end
        end

        default: state <= S_IDLE;
      endcase
    end
  end

endmodule
