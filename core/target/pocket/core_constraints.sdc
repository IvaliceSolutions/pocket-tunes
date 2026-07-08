#
# Pocket Tunes core constraints
#
# PLL instance path: apf_top instantiates core_top as `ic`; core_top
# instantiates pt_pll as `pll`. Clock groups:
#   [0] clk_sys_48   — bridge/loader/BRAM write side
#   [1] clk_vid_12   — video pipeline / BRAM read side
#   [2] clk_vid_12_90 — DDR output clock only (no logic)
#

set_clock_groups -asynchronous \
 -group { bridge_spiclk } \
 -group { clk_74a } \
 -group { clk_74b } \
 -group { ic|pll|pt_pll_inst|altera_pll_i|*[0].*|divclk } \
 -group { ic|pll|pt_pll_inst|altera_pll_i|*[1].*|divclk \
          ic|pll|pt_pll_inst|altera_pll_i|*[2].*|divclk }

derive_clock_uncertainty
