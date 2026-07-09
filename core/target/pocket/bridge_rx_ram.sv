// Bridge-write receive RAM: APF writes (data-slot pushes or 0x0180 target-read
// responses) aimed at one 256MB bridge region land here; the CPU reads words.
//
// Four 8-bit banks (reliable inference), loader CDC via agg23 data_loader.

`default_nettype none

module bridge_rx_ram #(
    parameter MASK_UPPER_4 = 4'h1,  // bridge region (bridgeaddr[31:28])
    parameter WORDS = 4096          // 16 KB
) (
    input wire clk_74a,
    input wire clk_sys,

    // APF bridge (clk_74a domain)
    input wire        bridge_wr,
    input wire        bridge_endian_little,
    input wire [31:0] bridge_addr,
    input wire [31:0] bridge_wr_data,

    // CPU read port (clk_sys domain), 32-bit words
    input  wire [$clog2(WORDS)-1:0] rd_word_addr,
    output wire [31:0]              rd_data
);

  wire        wr_en;
  wire [27:0] wr_addr;
  wire [ 7:0] wr_data;

  data_loader #(
      .ADDRESS_MASK_UPPER_4     (MASK_UPPER_4),
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

  wire in_range = (wr_addr < WORDS * 4);
  wire wr = wr_en && in_range;

  reg [7:0] mb0[0:WORDS-1];
  reg [7:0] mb1[0:WORDS-1];
  reg [7:0] mb2[0:WORDS-1];
  reg [7:0] mb3[0:WORDS-1];

  wire [$clog2(WORDS)-1:0] wr_word = wr_addr[$clog2(WORDS)+1:2];

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

  assign rd_data = {q3, q2, q1, q0};

endmodule
