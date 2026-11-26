// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DSTAR_TX_GAIN_H
#define DSTAR_TX_GAIN_H

#include <stdint.h>

// Use one linear gain per PCM frame so extra gain never clips individual
// samples before AMBE estimates pitch and spectral amplitudes. Do not
// attenuate the original microphone signal if no boost can fit.
static inline uint16_t dstar_tx_gain_limit_q8(const int16_t *pcm, unsigned count, uint16_t requested)
{
	if (requested <= 256u)
		return requested;
	uint32_t peak = 0;
	for (unsigned i = 0; i < count; i++) {
		int32_t sample = pcm[i];
		uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);
		if (magnitude > peak)
			peak = magnitude;
	}
	if (peak * requested <= 32767u * 256u)
		return requested;
	uint32_t limit = (32767u * 256u) / peak;
	return (uint16_t)(limit < 256u ? 256u : limit);
}

#endif
