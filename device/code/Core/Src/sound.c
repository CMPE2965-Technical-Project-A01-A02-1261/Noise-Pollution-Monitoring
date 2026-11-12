/* One second A-weighted sound level (LAeq,1s) from the DMM-4026-B-I2S MEMS
 * microphone.
 *
 * I2S1 runs as master receiver with 24-bit data in 32-bit slots. DMA fills a
 * circular buffer and the half/full transfer interrupts filter each half and
 * reduce it to running totals, so no audio is stored beyond the DMA buffer.
 *
 * Averaging: sound level is an energy quantity, so the one second value is
 * the mean of the squared (weighted) samples, converted to dB once at the end
 * of the window:
 *
 *     LAeq = 10 * log10( (1/N) * sum(a[i]^2) / ref^2 )
 *
 * Averaging dB values or absolute amplitudes would read low for anything that
 * fluctuates (a 1 s window with a 0.1 s horn blast, for example). Leq is
 * also what the server and the reference meter use, so per second readings
 * combine correctly into per minute and per session Leq.
 *
 * Weighting: the microphone outputs a large sub-audio drift (a few Hz, around
 * 87 dB worth of energy when measured on the device) on top of its DC
 * offset. Subtracting the window mean does not remove it, so every sample
 * goes through an IEC 61672 A-weighting filter before squaring. A-weighting
 * is what noise regulations and ISO 1996 use, is 0 dB at 1 kHz, and is about
 * -70 dB at 10 Hz, which removes the drift.
 *
 * The A-weighting filter is the analog response split into first order
 * sections (four high pass: 20.6, 20.6, 107.7, 737.9 Hz; two low pass:
 * 12194 Hz, prewarped), each mapped with the bilinear transform. Within
 * 0.7 dB of the IEC curve from 10 Hz to 12.5 kHz at 47.6 kHz sampling. Each
 * section needs one multiply by a Q16 constant, done with two 32-bit
 * multiplies since the Cortex-M0+ has no 64-bit multiply or FPU. Section gains
 * are left out and corrected once, in the dB conversion.
 *
 * The window is counted in samples, not in HAL ticks, so every reading covers
 * exactly the same stretch of audio and no samples are skipped or counted
 * twice between windows. */

#include "sound.h"

#include <math.h>

/* Slot the microphone drives. The mic puts its MSB right at the WS edge, so
 * I2S1 runs MSB-justified: in Philips mode the STM32 samples one bit late,
 * which drops the sign bit and doubles every value. In MSB-justified mode
 * the STM32's first slot is WS high, so the mic (LR tied to GND, transmits
 * while WS is low) is in slot 1. Set to 0 if LR is tied to VDD. */
#define SOUND_CHANNEL 1u

/* Frames (left + right slot pairs) per DMA half buffer. 256 frames is
 * about 5.4 ms at 47.6 kHz, so each interrupt is short. */
#define SOUND_FRAMES_PER_HALF 256u
/* Each 32-bit slot arrives as two half words: data[23:8], then data[7:0] << 8. */
#define SOUND_HALFWORDS_PER_FRAME 4u
#define SOUND_DMA_HALFWORDS (2u * SOUND_FRAMES_PER_HALF * SOUND_HALFWORDS_PER_FRAME)

/* The mic sends 24-bit words with 18 bits of precision (6 zero LSBs), so
 * samples are kept as 18-bit signed values. */
#define SOUND_FULL_SCALE 131072 /* 2^17 */

/* Fixed point scaling through the filter: samples are shifted up by
 * SOUND_FILTER_SHIFT for precision inside the filter, and the output is
 * shifted down by SOUND_OUT_SHIFT before squaring so a second of squares
 * fits in 64 bits. */
#define SOUND_FILTER_SHIFT 8
#define SOUND_OUT_SHIFT 6

/* Audio discarded after start while the filter settles from the mic's DC
 * offset and the mic finishes its 20 ms start up. */
#define SOUND_STARTUP_DISCARD_MS 200u

/* Readings waiting for the main loop. The main loop can block for 500 ms or
 * more (StatusResponse), so a few seconds of slack is kept. */
#define SOUND_QUEUE_SIZE 8u

/* If no window completes in this time the I2S/DMA has stalled; restart it. */
#define SOUND_STALL_TIMEOUT_MS 3000u

/* Speed up the per sample path even in -O0 Debug builds; it runs ~47k times
 * per second inside the DMA interrupt. */
#define SOUND_HOT __attribute__((optimize("O2")))
#define SOUND_HOT_INLINE __attribute__((optimize("O2"), always_inline))

/* IEC 61672 A-weighting pole frequencies (Hz). */
#define A_F1 20.598997
#define A_F2 107.65265
#define A_F3 737.86223
#define A_F4 12194.217

typedef struct {
	uint64_t sumsq;   /* sum of squared weighted samples */
	uint32_t n;
	uint32_t peak;    /* largest |weighted sample| */
	int32_t max;      /* raw sample extremes, for clipping and fault checks */
	int32_t min;
	uint32_t tick;
} Sound_Window;

/* One first order section, state and coefficient. High pass:
 *   y = (x - x1) + y1 - y1 * c,      c = 1 - p
 * Low pass:
 *   y = (x + x1) + y1 * p,           c = |p|, sign in negative */
typedef struct {
	int32_t x1;
	int32_t y1;
	uint32_t c;       /* Q16 */
	bool negative;
} Sound_Section;

static I2S_HandleTypeDef *sound_i2s;
static uint16_t dma_buf[SOUND_DMA_HALFWORDS];
static uint32_t sample_rate;
static uint32_t window_samples;

/* Filter, and the factor that turns mean square weighted output into mean
 * square at the input for a 1 kHz tone. */
static Sound_Section hp_sec[4];
static Sound_Section lp_sec[2];
static double gain_1k;
static bool prime_filter;

/* Running totals for the window in progress (interrupt context only). */
static Sound_Window acc;
static uint32_t discard_samples;

static volatile Sound_Window queue[SOUND_QUEUE_SIZE];
static volatile uint8_t queue_head;
static volatile uint8_t queue_tail;
static volatile uint8_t dropped_count;
static volatile uint32_t last_window_tick;
static volatile bool restart_needed;

/* Actual frame rate from the I2S prescaler. The 64 MHz clock does not divide
 * evenly, so 48 kHz requested gives 47.619 kHz. */
static uint32_t ComputeSampleRate(const I2S_HandleTypeDef *hi2s)
{
	uint32_t i2sclk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2S1);
	uint32_t pr = hi2s->Instance->I2SPR;
	uint32_t div = (pr & SPI_I2SPR_I2SDIV) >> SPI_I2SPR_I2SDIV_Pos;
	uint32_t odd = (pr & SPI_I2SPR_ODD) ? 1u : 0u;
	uint32_t channel_bits = (hi2s->Instance->I2SCFGR & SPI_I2SCFGR_CHLEN) ? 32u : 16u;
	uint32_t divider = channel_bits * 2u * (2u * div + odd);
	if (divider == 0u) {
		return 0u;
	}
	return (i2sclk + divider / 2u) / divider;
}

/* Bilinear transform pole for an analog pole at f Hz. */
static double BilinearPole(double f, double fs, bool prewarp)
{
	double w = 2.0 * M_PI * f;
	if (prewarp) {
		w = 2.0 * fs * tan(w / (2.0 * fs));
	}
	return (2.0 * fs - w) / (2.0 * fs + w);
}

/* |H|^2 of one section at normalised angular frequency w. */
static double SectionPower(double p, bool high_pass, double w)
{
	double cw = cos(w);
	double num = high_pass ? 2.0 - 2.0 * cw : 2.0 + 2.0 * cw;
	return num / (1.0 - 2.0 * p * cw + p * p);
}

/* Builds the A-weighting sections for the actual sample rate and works out
 * their combined power gain at 1 kHz, using the quantised coefficients. */
static void DesignFilter(double fs)
{
	const double hp_f[4] = {A_F1, A_F1, A_F2, A_F3};
	const double w1k = 2.0 * M_PI * 1000.0 / fs;
	double power = 1.0;

	for (int i = 0; i < 4; i++) {
		double p = BilinearPole(hp_f[i], fs, false);
		hp_sec[i].c = (uint32_t)lround((1.0 - p) * 65536.0);
		hp_sec[i].negative = false;
		power *= SectionPower(1.0 - hp_sec[i].c / 65536.0, true, w1k);
	}
	for (int i = 0; i < 2; i++) {
		double p = BilinearPole(A_F4, fs, true);
		lp_sec[i].c = (uint32_t)lround(fabs(p) * 65536.0);
		lp_sec[i].negative = p < 0.0;
		double pq = (lp_sec[i].negative ? -1.0 : 1.0) * lp_sec[i].c / 65536.0;
		power *= SectionPower(pq, false, w1k);
	}
	gain_1k = power;
}

/* y * c / 65536 for c < 65536 using only 32-bit multiplies. Exact floor. */
static inline SOUND_HOT_INLINE int32_t MulQ16(int32_t y, uint32_t c)
{
	int32_t hi = y >> 16;
	uint32_t lo = (uint32_t)y & 0xFFFFu;
	return hi * (int32_t)c + (int32_t)((lo * c) >> 16);
}

static inline SOUND_HOT_INLINE int32_t HighPass(Sound_Section *s, int32_t x)
{
	int32_t y = (x - s->x1) + s->y1 - MulQ16(s->y1, s->c);
	s->x1 = x;
	s->y1 = y;
	return y;
}

static inline SOUND_HOT_INLINE int32_t LowPass(Sound_Section *s, int32_t x)
{
	int32_t t = MulQ16(s->y1, s->c);
	int32_t y = (x + s->x1) + (s->negative ? -t : t);
	s->x1 = x;
	s->y1 = y;
	return y;
}

static void ResetWindow(void)
{
	acc.sumsq = 0;
	acc.n = 0;
	acc.peak = 0;
	acc.max = INT32_MIN;
	acc.min = INT32_MAX;
}

static void FinishWindow(void)
{
	uint8_t next_head = (uint8_t)((queue_head + 1u) % SOUND_QUEUE_SIZE);
	acc.tick = HAL_GetTick();
	last_window_tick = acc.tick;
	if (next_head == queue_tail) {
		dropped_count++;
	} else {
		queue[queue_head].sumsq = acc.sumsq;
		queue[queue_head].n = acc.n;
		queue[queue_head].peak = acc.peak;
		queue[queue_head].max = acc.max;
		queue[queue_head].min = acc.min;
		queue[queue_head].tick = acc.tick;
		queue_head = next_head;
	}
	ResetWindow();
}

/* Filters one DMA half buffer and adds it to the running totals. */
static SOUND_HOT void ProcessHalf(const uint16_t *buf)
{
	const uint16_t *p = buf + SOUND_CHANNEL * 2u;
	for (uint32_t i = 0; i < SOUND_FRAMES_PER_HALF; i++, p += SOUND_HALFWORDS_PER_FRAME) {
		/* Rebuild the 24-bit word at the top of an int32, then shift down
		 * arithmetically: 8 bits of padding plus the 6 unused LSBs. */
		int32_t s = (int32_t)(((uint32_t)p[0] << 16) | p[1]) >> 14;
		int32_t x = s * (1 << SOUND_FILTER_SHIFT);

		if (prime_filter) {
			/* Start from the current DC level instead of a step from 0. */
			hp_sec[0].x1 = x;
			prime_filter = false;
		}
		int32_t y = HighPass(&hp_sec[0], x);
		y = HighPass(&hp_sec[1], y);
		y = HighPass(&hp_sec[2], y);
		y = HighPass(&hp_sec[3], y);
		y = LowPass(&lp_sec[0], y);
		y = LowPass(&lp_sec[1], y);

		if (discard_samples > 0u) {
			discard_samples--;
			continue;
		}

		int32_t a = y >> SOUND_OUT_SHIFT;
		uint32_t mag = (uint32_t)(a < 0 ? -a : a);
		acc.sumsq += (uint64_t)((int64_t)a * a);
		if (mag > acc.peak) acc.peak = mag;
		if (s > acc.max) acc.max = s;
		if (s < acc.min) acc.min = s;

		if (++acc.n >= window_samples) {
			FinishWindow();
		}
	}
}

static bool StartCapture(void)
{
	ResetWindow();
	for (int i = 0; i < 4; i++) {
		hp_sec[i].x1 = 0;
		hp_sec[i].y1 = 0;
	}
	for (int i = 0; i < 2; i++) {
		lp_sec[i].x1 = 0;
		lp_sec[i].y1 = 0;
	}
	prime_filter = true;
	discard_samples = sample_rate * SOUND_STARTUP_DISCARD_MS / 1000u;
	last_window_tick = HAL_GetTick();
	restart_needed = false;
	/* Size counts 24-bit data items (both slots); HAL doubles it to get the
	 * number of half words the DMA moves. */
	return HAL_I2S_Receive_DMA(sound_i2s, dma_buf,
	                           (uint16_t)(SOUND_DMA_HALFWORDS / 2u)) == HAL_OK;
}

bool Sound_Start(I2S_HandleTypeDef *hi2s)
{
	sound_i2s = hi2s;
	sample_rate = ComputeSampleRate(hi2s);
	if (sample_rate == 0u) {
		return false;
	}
	window_samples = sample_rate; /* one second */
	DesignFilter((double)sample_rate);
	return StartCapture();
}

uint32_t Sound_GetSampleRate(void)
{
	return sample_rate;
}

uint8_t Sound_GetDroppedCount(void)
{
	return dropped_count;
}

/* Returns the oldest finished one second reading. Call from the main loop;
 * also restarts capture after an error or stall. */
bool Sound_Read(Sound_Reading *reading)
{
	if (sound_i2s == NULL) {
		return false;
	}
	/* Read the ISR's tick before the current tick so the difference can't
	 * go negative and wrap. */
	uint32_t last = last_window_tick;
	if (restart_needed || HAL_GetTick() - last > SOUND_STALL_TIMEOUT_MS) {
		HAL_I2S_DMAStop(sound_i2s);
		StartCapture();
	}

	if (reading == NULL || queue_tail == queue_head) {
		return false;
	}

	Sound_Window w;
	w.sumsq = queue[queue_tail].sumsq;
	w.n = queue[queue_tail].n;
	w.peak = queue[queue_tail].peak;
	w.max = queue[queue_tail].max;
	w.min = queue[queue_tail].min;
	w.tick = queue[queue_tail].tick;
	queue_tail = (uint8_t)((queue_tail + 1u) % SOUND_QUEUE_SIZE);

	/* Output units are input LSB * 2^(FILTER_SHIFT - OUT_SHIFT) * filter
	 * gain; refer the mean square back to the input at 1 kHz. */
	const double out_scale = (double)(1u << (SOUND_FILTER_SHIFT - SOUND_OUT_SHIFT));
	const double to_input = 1.0 / (out_scale * out_scale * gain_1k);
	double ms = (double)w.sumsq / (double)w.n * to_input;
	/* Never report below the 18-bit quantisation noise (1/12 LSB^2). */
	if (ms < 1.0 / 12.0) {
		ms = 1.0 / 12.0;
	}

	/* dBFS relative to full scale peak (FS^2), as the mic's sensitivity
	 * is specified. This made the datasheet's -26 dBFS match a reference
	 * meter to within 0.1 dB. */
	const double full_scale_ms = (double)SOUND_FULL_SCALE * SOUND_FULL_SCALE;
	const float spl_offset = 94.0f - SOUND_MIC_SENSITIVITY_DBFS + SOUND_CAL_OFFSET_DB;
	reading->db = 10.0f * log10f((float)(ms / full_scale_ms)) + spl_offset;

	/* Peak relative to the same reference; a steady sine reads Leq + 3 dB. */
	double peak = (double)w.peak * sqrt(to_input);
	if (peak < 1.0) {
		peak = 1.0;
	}
	reading->peak_db = 20.0f * log10f((float)(peak / SOUND_FULL_SCALE)) + spl_offset;

	reading->clipped = w.max >= SOUND_FULL_SCALE - 1 || w.min <= -SOUND_FULL_SCALE;
	reading->valid = w.max != w.min;
	reading->tick = w.tick;
	return true;
}

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
	if (hi2s == sound_i2s) {
		ProcessHalf(&dma_buf[0]);
	}
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
	if (hi2s == sound_i2s) {
		ProcessHalf(&dma_buf[SOUND_DMA_HALFWORDS / 2u]);
	}
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
	if (hi2s == sound_i2s) {
		/* Restarting here would call HAL functions that wait on SysTick;
		 * leave it to Sound_Read in the main loop. */
		restart_needed = true;
	}
}
