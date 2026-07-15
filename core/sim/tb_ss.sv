// Unit test for ss_ctrl: full save + load handshakes, including the 4-phase
// done/req exchange and the ok-clears-on-new-request rule.
`default_nettype none
`timescale 1ns/1ps
module tb_ss;
  reg clk = 0, reset_n = 0;
  always #6.7 clk = ~clk;  // ~74 MHz

  reg ss_start = 0, ss_load = 0, save_done = 0, load_done = 0;
  wire start_ack, start_busy, start_ok, start_err;
  wire load_ack, load_busy, load_ok, load_err;
  wire save_req, load_req;

  ss_ctrl dut (
      .clk_74a(clk), .reset_n(reset_n),
      .ss_start(ss_start), .ss_load(ss_load),
      .ss_start_ack(start_ack), .ss_start_busy(start_busy),
      .ss_start_ok(start_ok), .ss_start_err(start_err),
      .ss_load_ack(load_ack), .ss_load_busy(load_busy),
      .ss_load_ok(load_ok), .ss_load_err(load_err),
      .save_req(save_req), .save_done(save_done),
      .load_req(load_req), .load_done(load_done)
  );

  integer errors = 0;
  task chk(input cond, input [127:0] name);
    if (!cond) begin errors = errors + 1; $display("FAIL: %0s", name); end
  endtask

  // firmware model: done follows req after a delay, clears as soon as req drops
  reg [7:0] sctr = 0, lctr = 0;
  always @(posedge clk) begin
    if (!save_req) begin save_done <= 0; sctr <= 0; end
    else if (sctr < 20) sctr <= sctr + 1;
    else save_done <= 1;
    if (!load_req) begin load_done <= 0; lctr <= 0; end
    else if (lctr < 35) lctr <= lctr + 1;
    else load_done <= 1;
  end

  initial begin
    repeat (5) @(posedge clk);
    reset_n = 1;
    repeat (3) @(posedge clk);

    // ---- SAVE ----
    ss_start = 1; @(posedge clk); @(posedge clk);
    chk(start_ack || start_busy, "save: ack/busy after start");
    ss_start = 0;
    wait (start_ok);
    chk(!start_busy, "save: busy low at ok");
    chk(!save_req, "save: req dropped at ok");
    repeat (10) @(posedge clk);
    chk(save_done == 0, "save: fw done cleared after req drop");

    // ---- LOAD ----
    ss_load = 1; @(posedge clk); @(posedge clk);
    chk(load_ack || load_busy, "load: ack/busy after pulse");
    ss_load = 0;
    wait (load_ok);
    chk(!load_busy, "load: busy low at ok");
    repeat (10) @(posedge clk);

    // ---- second SAVE clears previous ok ----
    ss_start = 1; @(posedge clk); @(posedge clk); @(posedge clk);
    chk(!start_ok, "save2: ok cleared on new request");
    ss_start = 0;
    wait (start_ok);

    if (errors == 0) $display("PASS: ss_ctrl handshake ok");
    else $display("FAIL: %0d error(s)", errors);
    $finish;
  end

  initial begin #200000 $display("FAIL timeout"); $finish; end
endmodule
