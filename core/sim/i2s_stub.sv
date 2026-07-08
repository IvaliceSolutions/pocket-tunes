// LINT-ONLY stub of agg23 sound_i2s (real file uses decl-after-use, iverilog rejects it)
module sound_i2s #(
    parameter CHANNEL_WIDTH = 15,
    parameter SIGNED_INPUT  = 0
) (
    input wire clk_74a,
    input wire clk_audio,

    // Left and right audio channels. Can be in an arbitrary clock domain
    input wire [CHANNEL_WIDTH - 1:0] audio_l,
    input wire [CHANNEL_WIDTH - 1:0] audio_r,

    output reg audio_mclk,
    output reg audio_lrck,
    output reg audio_dac
);
endmodule
