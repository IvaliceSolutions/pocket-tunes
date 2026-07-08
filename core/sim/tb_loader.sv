// Testbench for library_slot: emulates APF bridge writes (32-bit words,
// big-endian, slot address 0x10000000, one word every ~75 clk_74a cycles like
// the real bridge), then reads the BRAM back through the video-domain port and
// checks both content and the bytes_loaded counter.
//
// Uses the real data_loader + bram_block_dp; only dcfifo is a behavioral stub.

`timescale 1ns / 1ps

module tb_loader;

  // clocks: 74.25 MHz bridge, 48 MHz sys, 12 MHz video-side read
  reg clk_74a = 0;
  always #6.734 clk_74a = ~clk_74a;
  reg clk_sys = 0;
  always #10.417 clk_sys = ~clk_sys;
  reg clk_rd = 0;
  always #41.666 clk_rd = ~clk_rd;

  reg        bridge_wr = 0;
  reg [31:0] bridge_addr = 0;
  reg [31:0] bridge_wr_data = 0;

  wire [16:0] rd_addr;
  wire [7:0] rd_data;
  wire [17:0] bytes_loaded;

  reg [16:0] rd_addr_r = 0;
  assign rd_addr = rd_addr_r;

  library_slot #(
      .ADDR_BITS(17)
  ) dut (
      .clk_74a(clk_74a),
      .clk_sys(clk_sys),

      .bridge_wr           (bridge_wr),
      .bridge_endian_little(1'b0),
      .bridge_addr         (bridge_addr),
      .bridge_wr_data      (bridge_wr_data),

      .rd_clk (clk_rd),
      .rd_addr(rd_addr),
      .rd_data(rd_data),

      .bytes_loaded(bytes_loaded)
  );

  // reference data: first N bytes of the real library.json
  localparam N_BYTES = 4096;
  reg [7:0] ref_mem[0:131071];

  integer i, errors = 0;
  reg [7:0] expect_byte;

  task bridge_write(input [31:0] addr, input [31:0] data);
    begin
      @(posedge clk_74a);
      bridge_addr    <= addr;
      bridge_wr_data <= data;
      bridge_wr      <= 1;
      @(posedge clk_74a);
      bridge_wr <= 0;
      // real bridge paces one word per ~75 cycles
      repeat (74) @(posedge clk_74a);
    end
  endtask

  initial begin
    $readmemh("library_bytes.hex", ref_mem);

    repeat (20) @(posedge clk_74a);

    // stream N_BYTES as big-endian 32-bit words, like APF with endian_little=0
    for (i = 0; i < N_BYTES; i = i + 4) begin
      bridge_write(32'h10000000 + i,
                   {ref_mem[i], ref_mem[i+1], ref_mem[i+2], ref_mem[i+3]});
    end

    // let the sys-domain FIFO drain
    repeat (200) @(posedge clk_sys);

    if (bytes_loaded !== N_BYTES) begin
      $display("FAIL bytes_loaded: got %0d want %0d", bytes_loaded, N_BYTES);
      errors = errors + 1;
    end else $display("ok   bytes_loaded = %0d", bytes_loaded);

    // read back every byte through the video-side port (1-cycle latency)
    for (i = 0; i < N_BYTES; i = i + 1) begin
      @(posedge clk_rd);
      rd_addr_r <= i[16:0];
      @(posedge clk_rd);
      @(negedge clk_rd);
      expect_byte = ref_mem[i];
      if (rd_data !== expect_byte) begin
        if (errors < 10)
          $display("FAIL byte %0d: got %02x want %02x", i, rd_data, expect_byte);
        errors = errors + 1;
      end
    end

    if (errors == 0) $display("ALL LOADER CHECKS PASSED (%0d bytes verified)", N_BYTES);
    else $display("%0d ERRORS", errors);
    $finish;
  end

  initial begin
    #80000000;
    $display("FAIL timeout");
    $finish;
  end

endmodule
