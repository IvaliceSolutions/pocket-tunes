/* M7e memory layout — code in the 256 KB external async SRAM, the hottest
 * decode kernels of the ACTIVE codec in a 16 KB instruction TCM (1-cycle
 * BRAM), data in the 128 KB BRAM.
 *
 * Why the TCM: an I-cache miss refills a 32 B line from the 55 ns SRAM in ~80
 * clk_sys cycles; both codecs' working sets thrash the 4 KB cache, so decode
 * ran at/below real time (opus ~0.95x, MP3 ~1.0x razor-thin → crackle). The
 * profile-dominant kernels refill from BRAM at ~1 cycle/word instead.
 *
 * OVERLAY: opus and MP3 never play at once, so their hot kernels share the one
 * 16 KB TCM. codec_start() copies the active codec's overlay to 0x6000_0000
 * (itcm_load_opus / itcm_load_mp3, from the LMAs below) before decoding. Both
 * overlays stay under 16 KB, so no fetch ever sets addr bit 15 (the unresolved
 * >32 KB TCM bug). crt0 no longer pre-copies the TCM.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
  SRAM (rx)  : ORIGIN = 0x50000000, LENGTH = 256K
  ITCM (rwx) : ORIGIN = 0x60000000, LENGTH = 16K
  RAM  (rwx) : ORIGIN = 0x00000000, LENGTH = 128K
}
SECTIONS {
  /* .text.init (crt0 _start) MUST lead: SRAM origin 0x5000_0000 is the reset
   * vector. The overlaid codec objects are excluded here and claimed by the
   * OVERLAY below; .text keeps the LMA at SRAM origin so the reset vector
   * stays crt0. */
  .text : {
    *(.text.init)
    EXCLUDE_FILE(
      *opus_kiss_fft.o *opus_mdct.o *opus_bands.o *opus_vq.o *opus_cwrs.o
      *opus_entdec.o *opus_celt_decoder.o
      *helix_polyphase.o *helix_imdct.o *helix_dct32.o *helix_subband.o
    ) *(.text*)
  } > SRAM

  /* Opus cold tables: SRAM-resident, read uncached via the dBus */
  .sramdata : { *(.sram.rodata*) } > SRAM

  /* fast rodata + data: run from BRAM, load image in SRAM (crt0 copies) */
  .rodata : { __fast_start = .; *(.rodata*) *(.srodata*) } > RAM AT> SRAM
  .data   : { *(.data*) *(.sdata*) ; __fast_end = .; } > RAM AT> SRAM
  __fast_lma = LOADADDR(.rodata);

  /* Overlay: both codecs' hot kernels at VMA 0x6000_0000, separate LMAs at the
   * END of the SRAM image (past text/rodata/data so nothing overlaps).
   * codec_start() word-copies the active one to the TCM (I-cache flushed). */
  __image_end_lma = __fast_lma + (__fast_end - __fast_start);
  OVERLAY 0x60000000 : NOCROSSREFS AT ( __image_end_lma ) {
    /* PROFILE-DRIVEN pin (host sampling of the CELT stereo demo): the
     * per-SAMPLE loops live here; the per-frame skeletons (quant_all_bands,
     * compute_theta, celt_synthesis, comb_filter) stay in SRAM where the 4 KB
     * I-cache handles their smaller residual working set. 15.8 KB / 16 KB. */
    .itcm_opus {
      *opus_kiss_fft.o(.text .text.*)                 /* fft_impl: 4.5 KB, hottest */
      *opus_mdct.o(.text.clt_mdct_backward_c)
      *opus_bands.o(.text.quant_band_stereo)
      *opus_bands.o(.text.quant_partition)
      *opus_bands.o(.text.quant_band)
      *opus_bands.o(.text.denormalise_bands)
      *opus_vq.o(.text.alg_unquant .text.exp_rotation .text.exp_rotation.* .text.exp_rotation1 .text.normalise_residual .text.renormalise_vector)
      *opus_cwrs.o(.text.decode_pulses)
      *opus_entdec.o(.text .text.*)                   /* range decoder, all hot */
      *opus_celt_decoder.o(.text.deemphasis)
    }
    .itcm_mp3 {
      *helix_polyphase.o(.text .text.*)
      *helix_imdct.o(.text .text.*)
      *helix_dct32.o(.text .text.*)
      *helix_subband.o(.text .text.*)
    }
  }
  __itcm_load_opus = LOADADDR(.itcm_opus);
  __itcm_load_mp3  = LOADADDR(.itcm_mp3);
  __itcm_opus_size = SIZEOF(.itcm_opus);
  __itcm_mp3_size  = SIZEOF(.itcm_mp3);

  /* Cold leftovers of the function-split objects (encode-side already gc'd;
   * per-frame skeletons): execute from SRAM like the rest of .text. */
  __cold_lma = __image_end_lma + SIZEOF(.itcm_opus) + SIZEOF(.itcm_mp3);
  .text_cold __cold_lma : AT ( __cold_lma ) {
    *opus_mdct.o(.text .text.*)
    *opus_bands.o(.text .text.*)
    *opus_vq.o(.text .text.*)
    *opus_cwrs.o(.text .text.*)
    *opus_celt_decoder.o(.text .text.*)
  }

  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) ; __bss_end = .; } > RAM

  _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
