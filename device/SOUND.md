# Sound level measurement

How the device turns microphone samples into one dB reading per second.

## Hardware and I2S setup
- Mic: PUI Audio DMM-4026-B-I2S (24-bit word in a 32-bit slot, 18-bit precision, 6 always-zero LSBs).
- STM32G0B1 I2S1 master receive, 24-bit data / 32-bit channel, MCLK off.
- **MSB-justified framing** (not Philips). In Philips mode the STM32 read every word
  one bit late: 7 zero LSBs instead of 6, the sign bit lost, and every value doubled (+6.02 dB).
- In MSB-justified mode the mic (LR tied to GND, transmits while WS is low) is in **slot 1** (`SOUND_CHANNEL 1`).
- Sample rate: 48 kHz requested, **47.619 kHz actual** (64 MHz / (64 × 21)).
  BCLK ≈ 3.05 MHz, inside the mic's 2.048–4.096 MHz range (32 kHz gave ~2.0 MHz, below it).
- Circular DMA, 256 frames per half buffer (~5.4 ms). Each half is processed in the DMA interrupt
  (NVIC priority 2, so USART1 at priority 0 can interrupt it).

## Sample decoding
Each slot arrives as two half-words: `data[23:8]`, then `data[7:0] << 8`.
`s = (int32_t)((hw0 << 16) | hw1) >> 14` gives a signed 18-bit sample (full scale 2^17).

## Why A-weighting
The mic outputs a large, slow drift (a few Hz, around ±3000 LSB) worth about 87 dB on its own.
Subtracting the window mean does not remove it, so every sample goes through an
**IEC 61672 A-weighting** filter before squaring:
- 4 first-order high-pass sections (20.6, 20.6, 107.7, 737.9 Hz) and 2 low-pass sections
  (12194 Hz, prewarped), each mapped with the bilinear transform.
- Q16 fixed-point coefficients, computed at boot from the actual sample rate; the multiplies use
  only 32-bit operations (Cortex-M0+ has no FPU or 64-bit multiply).
- Within 0.7 dB of the IEC curve from 10 Hz to 12.5 kHz; 0 dB at 1 kHz.
- Section gains are left out and corrected once at 1 kHz in the dB conversion.

## One-second average (LAeq,1s)
Sound level is an energy quantity, so the reading is the **mean of the squared weighted samples**,
converted to dB once per window:

    LAeq = 10·log10( (1/N)·Σ a[i]² / FS² ) + 94 − S + CAL

- N = 47,619 samples (counted from the I2S clock, not timer ticks), so every window is exactly
  one second of audio with no gaps or overlap.
- FS = 2^17; dBFS is relative to full-scale peak. S = −26 dBFS (mic sensitivity at 94 dB SPL),
  CAL = `SOUND_CAL_OFFSET_DB` = 0.
- Averaging dB values instead under-reads fluctuating noise: a 0.1 s 100 dB burst in 50 dB reads
  90.0 dB as Leq but 55 dB as a mean of dB values. The server's Leq/L10/L50/L90 depend on energy averaging.
- Floor clamp: the 18-bit quantisation noise (1/12 LSB²).
- Peak: largest |weighted sample|, in the same units (a steady sine reads Leq + 3 dB).
- `clipped`: a raw sample reached ±full scale. `valid`: false when the mic sent a constant value.
- The first 200 ms after start is discarded while the filter settles.

## Verification
- Host tests with synthetic I2S data: 94 dB tone → 94.00; 60/40 dB tones under the measured drift → 60.02/40.03;
  frequency sweep matches the IEC table.
- On hardware: speech near the mic matches a reference at ~70 dBA. A quiet room (~40 dBA) reads 3–4 dB high
  against the reference. A clean raw capture showed that energy is real sound: the device level matched the
  raw data, no electrical tones were found, and the mic's own noise is at most 31.8 dBA (≤0.6 dB at 40 dBA).
  The remaining gap is likely from the reference meter or how it was read; compare LAeq over 30–60 s with
  both mics side by side.
