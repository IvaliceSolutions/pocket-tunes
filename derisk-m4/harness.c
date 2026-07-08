/* Bare-metal rv32im harness: decode a real MP3 with Helix and report the exact
 * retired-instruction cost per second of audio (via the minstret CSR under qemu).
 * This quantifies M4 feasibility: required RISC-V clock for real-time MP3. */

#include <stdint.h>
#include "mp3dec.h"

extern unsigned char test128_mp3[];
extern unsigned int  test128_mp3_len;

/* ---- minimal freestanding libc for Helix ---- */
extern char _heap_start[], _heap_end[];
static char *hp = _heap_start;
void *malloc(unsigned long n) {
  n = (n + 15u) & ~15u;
  if (hp + n > _heap_end) return 0;
  void *p = hp; hp += n; return p;
}
void free(void *p) { (void)p; }            /* bump allocator: no reclaim */
void *memset(void *d, int c, unsigned long n){ char*p=d; while(n--)*p++=(char)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n){ char*p=d; const char*q=s; while(n--)*p++=*q++; return d; }
void *memmove(void *d, const void *s, unsigned long n){
  char*p=d; const char*q=s;
  if (p<q) while(n--)*p++=*q++;
  else { p+=n; q+=n; while(n--)*--p=*--q; }
  return d;
}

/* ---- qemu 'virt' NS16550 UART @ 0x10000000 ---- */
#define UART ((volatile uint8_t *)0x10000000)
static void putc_(char c){ if(c=='\n') putc_('\r'); UART[0]=(uint8_t)c; }
static void puts_(const char*s){ while(*s) putc_(*s++); }
static void putu64(uint64_t v){ char b[24]; int i=0; if(!v){putc_('0');return;}
  while(v){ b[i++]='0'+(int)(v%10); v/=10; } while(i) putc_(b[--i]); }

/* ---- minstret (M-mode, rv32: read hi/lo/hi to avoid rollover tear) ---- */
static uint64_t rd_minstret(void){
  uint32_t hi, lo, hi2;
  do {
    __asm__ volatile("csrr %0, minstreth":"=r"(hi));
    __asm__ volatile("csrr %0, minstret" :"=r"(lo));
    __asm__ volatile("csrr %0, minstreth":"=r"(hi2));
  } while (hi != hi2);
  return ((uint64_t)hi << 32) | lo;
}

static short pcm[2 * 1152 * 2];   /* max stereo samples per MP3 frame, with margin */

int main(void){
  puts_("MAIN_START\n");
  HMP3Decoder h = MP3InitDecoder();
  if (!h){ puts_("init failed\n"); return 0; }
  puts_("INIT_OK\n");
  int firstSync = MP3FindSyncWord(test128_mp3, (int)test128_mp3_len);
  puts_("first_sync_off="); putu64((uint64_t)(uint32_t)firstSync); puts_("\n");

  unsigned char *inbuf = test128_mp3;
  int bytesLeft = (int)test128_mp3_len;
  long totalSamps = 0; int frames = 0, nChans = 2, sampRate = 44100;

  int errCount = 0, firstErr = 999;
  uint64_t t0 = rd_minstret();
  for (int guard = 0; guard < 20000 && bytesLeft > 4; guard++){
    int off = MP3FindSyncWord(inbuf, bytesLeft);
    if (off < 0) break;
    inbuf += off; bytesLeft -= off;
    unsigned char *before = inbuf;
    int err = MP3Decode(h, &inbuf, &bytesLeft, pcm, 0);
    if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) break;
    if (err){
      if (firstErr == 999) firstErr = err;
      errCount++;
      if (inbuf == before){ inbuf++; bytesLeft--; }
      continue;
    }
    MP3FrameInfo fi; MP3GetLastFrameInfo(h, &fi);
    totalSamps += fi.outputSamps; frames++;
    nChans = fi.nChans; sampRate = fi.samprate;
  }
  puts_("errCount="); putu64(errCount); puts_("  firstErr=");
  putu64((uint64_t)(uint32_t)firstErr); puts_("\n");
  uint64_t t1 = rd_minstret();
  uint64_t instr = t1 - t0;

  long samplesPerChan = nChans ? totalSamps / nChans : 0;
  /* instructions per second-of-audio = instr * sampRate / samplesPerChan */
  uint64_t ips = samplesPerChan ? (instr * (uint64_t)sampRate) / (uint64_t)samplesPerChan : 0;

  puts_("frames=");      putu64(frames);
  puts_("  instr=");     putu64(instr);
  puts_("  chans=");     putu64(nChans);
  puts_("  samprate=");  putu64(sampRate);
  puts_("\nsamples_per_chan="); putu64(samplesPerChan);
  puts_("  audio_ms=");  putu64((uint64_t)samplesPerChan * 1000 / (sampRate?sampRate:1));
  puts_("\ninstr_per_audio_second="); putu64(ips);
  puts_("\nreq_MHz_at_IPC_1.0="); putu64(ips / 1000000);
  puts_("\nreq_MHz_at_IPC_0.7="); putu64((ips * 10 / 7) / 1000000);
  puts_("\nreq_MHz_at_IPC_0.5="); putu64((ips * 2) / 1000000);
  puts_("\n");
  MP3FreeDecoder(h);
  return 0;
}
