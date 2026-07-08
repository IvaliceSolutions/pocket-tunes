// library.json data slot: APF bridge writes → 128 KB BRAM + byte counter.
//
// APF streams the file as 32-bit bridge writes at slot address 0x1000_0000;
// data_loader (agg23) splits them into bytes in the clk_sys domain. Writes are
// sequential, so `bytes_loaded` = last written address + 1 = file size.
// Port B is the video-domain read side.

`default_nettype none

module library_slot #(
    parameter ADDR_BITS = 17  // 128 KB
) (
    input wire clk_74a,
    input wire clk_sys,

    // APF bridge (clk_74a domain)
    input wire        bridge_wr,
    input wire        bridge_endian_little,
    input wire [31:0] bridge_addr,
    input wire [31:0] bridge_wr_data,

    // read port (its own clock domain)
    input  wire                 rd_clk,
    input  wire [ADDR_BITS-1:0] rd_addr,
    output wire [          7:0] rd_data,

    // file size so far (clk_sys domain)
    output reg [17:0] bytes_loaded
);

  wire        wr_en;
  wire [27:0] wr_addr;
  wire [ 7:0] wr_data;

  data_loader #(
      .ADDRESS_MASK_UPPER_4     (4'h1),  // slot address 0x10000000
      .ADDRESS_SIZE             (28),
      .WRITE_MEM_CLOCK_DELAY    (4),
      .WRITE_MEM_EN_CYCLE_LENGTH(1),
      .OUTPUT_WORD_SIZE         (1)
  ) loader (
      .clk_74a   (clk_74a),
      .clk_memory(clk_sys),

      .bridge_wr           (bridge_wr),
      .bridge_endian_little(bridge_endian_little),
      .bridge_addr         (bridge_addr),
      .bridge_wr_data      (bridge_wr_data),

      .write_en  (wr_en),
      .write_addr(wr_addr),
      .write_data(wr_data)
  );

  wire in_range = (wr_addr[27:ADDR_BITS] == 0);

  bram_block_dp #(
      .DATA(8),
      .ADDR(ADDR_BITS)
  ) bram (
      .a_clk (clk_sys),
      .a_wr  (wr_en && in_range),
      .a_addr(wr_addr[ADDR_BITS-1:0]),
      .a_din (wr_data),
      .a_dout(),

      .b_clk (rd_clk),
      .b_wr  (1'b0),
      .b_addr(rd_addr),
      .b_din (8'd0),
      .b_dout(rd_data)
  );

  initial bytes_loaded = 0;
  always @(posedge clk_sys) begin
    if (wr_en && in_range) begin
      bytes_loaded <= {1'b0, wr_addr[ADDR_BITS-1:0]} + 18'd1;
    end
  end

endmodule
