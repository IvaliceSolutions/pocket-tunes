// 400x360 @ 60 Hz video timing generator for the Pocket scaler (4x → 1600x1440).
//
// Dot clock 12 MHz. Totals: 500 x 400 = 200,000 dots/frame = exactly 60 Hz.
// Line layout : [0..399] active, [400..407] front porch, [408..439] HS, [440..499] back porch
// Frame layout: [0..359] active, [360..362] front porch, [363..365] VS, [366..399] back porch
//
// Pure synchronous RTL (no vendor primitives) so it simulates under iverilog.

`default_nettype none

module video_gen #(
    parameter H_ACTIVE = 400,
    parameter H_FP     = 8,
    parameter H_SYNC   = 32,
    parameter H_BP     = 60,
    parameter V_ACTIVE = 360,
    parameter V_FP     = 3,
    parameter V_SYNC   = 3,
    parameter V_BP     = 34
) (
    input wire clk,      // dot clock
    input wire reset_n,

    output reg  de,          // active-picture enable (registered, aligned with x/y)
    output reg  hs,          // horizontal sync pulse (active high)
    output reg  vs,          // vertical sync pulse (active high)
    output reg  de_falling,  // 1-cycle strobe on the cycle right after DE ends (video slot word)
    output wire [9:0] x,     // pixel x when de (0..H_ACTIVE-1)
    output wire [9:0] y,     // pixel y when de (0..V_ACTIVE-1)
    output wire [9:0] next_x, // lookahead combinational position (for 1-cycle-latency fetches)
    output wire [9:0] next_y,
    output reg  frame_start  // 1-cycle strobe at (0,0) of active video
);

  localparam H_TOTAL = H_ACTIVE + H_FP + H_SYNC + H_BP;  // 500
  localparam V_TOTAL = V_ACTIVE + V_FP + V_SYNC + V_BP;  // 400

  reg [9:0] hcnt = 0;
  reg [9:0] vcnt = 0;

  assign x = hcnt;
  assign y = vcnt;

  // Position one dot ahead (wrapping), presented to memories so their
  // registered read data lands exactly when de/x/y reach that dot.
  assign next_x = (hcnt == H_TOTAL - 1) ? 10'd0 : hcnt + 10'd1;
  assign next_y = (hcnt == H_TOTAL - 1) ? ((vcnt == V_TOTAL - 1) ? 10'd0 : vcnt + 10'd1) : vcnt;

  wire de_next = (next_x < H_ACTIVE) && (next_y < V_ACTIVE);

  always @(posedge clk) begin
    if (~reset_n) begin
      hcnt <= 0;
      vcnt <= 0;
      de <= 0;
      hs <= 0;
      vs <= 0;
      de_falling <= 0;
      frame_start <= 0;
    end else begin
      hcnt <= next_x;
      vcnt <= next_y;

      de_falling <= de && !de_next;
      de <= de_next;
      hs <= (next_x >= H_ACTIVE + H_FP) && (next_x < H_ACTIVE + H_FP + H_SYNC);
      vs <= (next_y >= V_ACTIVE + V_FP) && (next_y < V_ACTIVE + V_FP + V_SYNC);
      frame_start <= (next_x == 0) && (next_y == 0);
    end
  end

endmodule
