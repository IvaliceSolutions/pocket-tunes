// library.json data slot: APF bridge writes → 128 KB RAM + byte counter.
//
// Storage is 32-bit wide (4 pixels of JSON text per word…) so the CPU reads
// full words; the APF loader delivers single bytes, steered to the right
// byte lane. Loader writes and CPU reads share clk_sys (single clock).
// `bytes_loaded` = last written address + 1 = file size (writes sequential).

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

    // CPU read port (clk_sys domain), 32-bit words
    input  wire [ADDR_BITS-3:0] rd_word_addr,
    output reg  [31:0]          rd_data,

    // file size so far (clk_sys domain)
    output reg [17:0] bytes_loaded
);

  localparam WORDS = 1 << (ADDR_BITS - 2);

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
  wire wr = wr_en && in_range;

  // four 8-bit banks (one per byte lane) — reliable RAM inference; the
  // loader's byte stream steers into one bank by wr_addr[1:0]
  reg [7:0] mb0[0:WORDS-1];
  reg [7:0] mb1[0:WORDS-1];
  reg [7:0] mb2[0:WORDS-1];
  reg [7:0] mb3[0:WORDS-1];

  wire [ADDR_BITS-3:0] wr_word = wr_addr[ADDR_BITS-1:2];

  reg [7:0] q0, q1, q2, q3;

  always @(posedge clk_sys) begin
    if (wr && wr_addr[1:0] == 2'd0) mb0[wr_word] <= wr_data;
    q0 <= mb0[rd_word_addr];
  end
  always @(posedge clk_sys) begin
    if (wr && wr_addr[1:0] == 2'd1) mb1[wr_word] <= wr_data;
    q1 <= mb1[rd_word_addr];
  end
  always @(posedge clk_sys) begin
    if (wr && wr_addr[1:0] == 2'd2) mb2[wr_word] <= wr_data;
    q2 <= mb2[rd_word_addr];
  end
  always @(posedge clk_sys) begin
    if (wr && wr_addr[1:0] == 2'd3) mb3[wr_word] <= wr_data;
    q3 <= mb3[rd_word_addr];
  end

  always @(*) rd_data = {q3, q2, q1, q0};

  initial bytes_loaded = 0;
  always @(posedge clk_sys) begin
    if (wr) bytes_loaded <= {1'b0, wr_addr[ADDR_BITS-1:0]} + 18'd1;
  end

endmodule
