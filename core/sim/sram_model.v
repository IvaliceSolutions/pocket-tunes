// Behavioral async SRAM model (AS6C2016-ish: 128K x 16).
// Read: dq driven while oe_n=0 & we_n=1. Write: data latched on we_n rising
// edge (address/data assumed stable during the low pulse, as the controller
// guarantees). Optionally preloaded from a little-endian halfword hex file.
`default_nettype none
`timescale 1ns/1ps

module sram_model #(
    parameter PRELOAD = ""  // firmware_sram.hex (one 4-hex-digit halfword per line)
) (
    input  wire [16:0] a,
    inout  wire [15:0] dq,
    input  wire        oe_n,
    input  wire        we_n,
    input  wire        ub_n,
    input  wire        lb_n
);

  reg [15:0] mem[0:131071];

  initial begin
    if (PRELOAD != "") $readmemh(PRELOAD, mem);
  end

  assign dq = (!oe_n && we_n) ? mem[a] : 16'hZZZZ;

  always @(posedge we_n) begin
    if (!ub_n) mem[a][15:8] <= dq[15:8];
    if (!lb_n) mem[a][7:0] <= dq[7:0];
  end

endmodule
