// Pocket Tunes PLL
//
// 74.25 MHz reference in (Pocket clk_74a) →
//   outclk_0: 48 MHz  clk_sys   (bridge consumers now; soft-CPU domain later)
//   outclk_1: 12 MHz  clk_vid   (dot clock, 400x360@60: 500x400 total = 200k dots)
//   outclk_2: 12 MHz  clk_vid_90 (+90° = 20833 ps, for video_rgb_clock_90)
//
// Same construction as the Analogue/agg23 template PLLs: a thin wrapper around
// altera_pll with explicit frequency-string parameters (synthesizable as-is,
// no Quartus IP regeneration needed).

`timescale 1 ps / 1 ps
module pt_pll (
    input  wire refclk,
    input  wire rst,
    output wire outclk_0,
    output wire outclk_1,
    output wire outclk_2,
    output wire locked
);

  pt_pll_0002 pt_pll_inst (
      .refclk  (refclk),
      .rst     (rst),
      .outclk_0(outclk_0),
      .outclk_1(outclk_1),
      .outclk_2(outclk_2),
      .locked  (locked)
  );

endmodule

module pt_pll_0002 (
    input  wire refclk,
    input  wire rst,
    output wire outclk_0,
    output wire outclk_1,
    output wire outclk_2,
    output wire locked
);

  altera_pll #(
      .fractional_vco_multiplier("true"),
      .reference_clock_frequency("74.25 MHz"),
      .operation_mode("normal"),
      .number_of_clocks(3),
      .output_clock_frequency0("48.000000 MHz"),
      .phase_shift0("0 ps"),
      .duty_cycle0(50),
      .output_clock_frequency1("12.000000 MHz"),
      .phase_shift1("0 ps"),
      .duty_cycle1(50),
      .output_clock_frequency2("12.000000 MHz"),
      .phase_shift2("20833 ps"),
      .duty_cycle2(50),
      .output_clock_frequency3("0 MHz"),
      .phase_shift3("0 ps"),
      .duty_cycle3(50),
      .output_clock_frequency4("0 MHz"),
      .phase_shift4("0 ps"),
      .duty_cycle4(50),
      .output_clock_frequency5("0 MHz"),
      .phase_shift5("0 ps"),
      .duty_cycle5(50),
      .output_clock_frequency6("0 MHz"),
      .phase_shift6("0 ps"),
      .duty_cycle6(50),
      .output_clock_frequency7("0 MHz"),
      .phase_shift7("0 ps"),
      .duty_cycle7(50),
      .output_clock_frequency8("0 MHz"),
      .phase_shift8("0 ps"),
      .duty_cycle8(50),
      .output_clock_frequency9("0 MHz"),
      .phase_shift9("0 ps"),
      .duty_cycle9(50),
      .output_clock_frequency10("0 MHz"),
      .phase_shift10("0 ps"),
      .duty_cycle10(50),
      .output_clock_frequency11("0 MHz"),
      .phase_shift11("0 ps"),
      .duty_cycle11(50),
      .output_clock_frequency12("0 MHz"),
      .phase_shift12("0 ps"),
      .duty_cycle12(50),
      .output_clock_frequency13("0 MHz"),
      .phase_shift13("0 ps"),
      .duty_cycle13(50),
      .output_clock_frequency14("0 MHz"),
      .phase_shift14("0 ps"),
      .duty_cycle14(50),
      .output_clock_frequency15("0 MHz"),
      .phase_shift15("0 ps"),
      .duty_cycle15(50),
      .output_clock_frequency16("0 MHz"),
      .phase_shift16("0 ps"),
      .duty_cycle16(50),
      .output_clock_frequency17("0 MHz"),
      .phase_shift17("0 ps"),
      .duty_cycle17(50),
      .pll_type("General"),
      .pll_subtype("General")
  ) altera_pll_i (
      .rst     (rst),
      .outclk  ({outclk_2, outclk_1, outclk_0}),
      .locked  (locked),
      .fboutclk(),
      .fbclk   (1'b0),
      .refclk  (refclk)
  );
endmodule
