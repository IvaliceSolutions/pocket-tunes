// Savestate/sleep handshake controller (clk_74a domain).
//
// Bridges the APF savestate protocol (core_bridge_cmd: start/load pulses,
// ack/busy/ok/err back) to a slow firmware over a 4-phase handshake:
//
//   host pulse ──► req=1 ──► firmware serializes/applies, sets done=1
//   ok=1, req=0 ◄── done seen ── ; firmware clears done when req drops.
//
// The state block itself moves through pt_soc (param RAM window for reads,
// capture registers for writes) — this module only sequences the protocol.
// Per core_bridge_cmd's contract: ack ≥1 cycle after the pulse, busy while
// in progress, then ok held until the next request starts.

`default_nettype none

module ss_ctrl (
    input wire clk_74a,
    input wire reset_n,

    // core_bridge_cmd savestate interface
    input  wire ss_start,  // host wants a savestate created
    input  wire ss_load,   // host wrote a state block, apply it
    output reg  ss_start_ack,
    output reg  ss_start_busy,
    output reg  ss_start_ok,
    output reg  ss_start_err,
    output reg  ss_load_ack,
    output reg  ss_load_busy,
    output reg  ss_load_ok,
    output reg  ss_load_err,

    // firmware side (levels; done inputs already synchronized to clk_74a)
    output reg  save_req,
    input  wire save_done,
    output reg  load_req,
    input  wire load_done
);

  reg start_q, load_q;

  always @(posedge clk_74a or negedge reset_n) begin
    if (~reset_n) begin
      start_q <= 0;
      load_q <= 0;
      save_req <= 0;
      load_req <= 0;
      ss_start_ack <= 0; ss_start_busy <= 0; ss_start_ok <= 0; ss_start_err <= 0;
      ss_load_ack <= 0; ss_load_busy <= 0; ss_load_ok <= 0; ss_load_err <= 0;
    end else begin
      start_q <= ss_start;
      load_q  <= ss_load;
      ss_start_ack <= 0;
      ss_load_ack <= 0;

      // ---- create (sleep) ----
      if (ss_start && !start_q && !save_req && !save_done) begin  // full 4-phase before re-arm
        ss_start_ack  <= 1;
        ss_start_busy <= 1;
        ss_start_ok   <= 0;
        ss_start_err  <= 0;
        save_req      <= 1;
      end else if (save_req && save_done) begin
        save_req      <= 0;   // firmware wrote the block into the param window
        ss_start_busy <= 0;
        ss_start_ok   <= 1;
      end

      // ---- load (wake) ----
      if (ss_load && !load_q && !load_req && !load_done) begin
        ss_load_ack  <= 1;
        ss_load_busy <= 1;
        ss_load_ok   <= 0;
        ss_load_err  <= 0;
        load_req     <= 1;
      end else if (load_req && load_done) begin
        load_req     <= 0;    // firmware consumed the captured block
        ss_load_busy <= 0;
        ss_load_ok   <= 1;
      end
    end
  end

endmodule
