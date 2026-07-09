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

  reg [31:0] mem[0:WORDS-1];

  wire [ADDR_BITS-3:0] wr_word = wr_addr[ADDR_BITS-1:2];

  always @(posedge clk_sys) begin
    if (wr) begin
      case (wr_addr[1:0])
        2'd0: mem[wr_word][7:0] <= wr_data;
        2'd1: mem[wr_word][15:8] <= wr_data;
        2'd2: mem[wr_word][23:16] <= wr_data;
        2'd3: mem[wr_word][31:24] <= wr_data;
      endcase
    end
    rd_data <= mem[rd_word_addr];
  end

  initial bytes_loaded = 0;
  always @(posedge clk_sys) begin
    if (wr) bytes_loaded <= {1'b0, wr_addr[ADDR_BITS-1:0]} + 18'd1;
  end

endmodule
