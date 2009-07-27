/*
 * Copyright (C) 2007 - 2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 *
 */

#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include "cpcap_audio_driver.h"
#include <linux/spi/cpcap.h>
#include <mach/resource.h>
#include <linux/regulator/consumer.h>

#define SLEEP_ACTIVATE_POWER 2

#define CLOCK_TREE_RESET_TIME 1

#define SLEEP_CODEC_STDAC_PGA_SWITCH 10

#define SLEEP_OUTPUT_AMPS_ENABLE 10

#ifdef CPCAP_AUDIO_DEBUG
#define CPCAP_AUDIO_DEBUG_LOG(args...)  \
				printk(KERN_INFO "CPCAP_AUDIO_DRIVER:" args)
#else
#define CPCAP_AUDIO_DEBUG_LOG(args...)
#endif

#define CPCAP_AUDIO_ERROR_LOG(args...)  \
			printk(KERN_ERR "CPCAP_AUDIO_DRIVER: Error " args)

#define ERROR_EXIT _err
#define TRY(a)  if (unlikely(a)) goto ERROR_EXIT;

static struct cpcap_audio_state previous_state_struct = {
	NULL,
	CPCAP_AUDIO_MODE_NORMAL,
	CPCAP_AUDIO_CODEC_OFF,
	CPCAP_AUDIO_CODEC_RATE_8000_HZ,
	CPCAP_AUDIO_CODEC_MUTE,
	CPCAP_AUDIO_STDAC_OFF,
	CPCAP_AUDIO_STDAC_RATE_8000_HZ,
	CPCAP_AUDIO_STDAC_MUTE,
	CPCAP_AUDIO_ANALOG_SOURCE_OFF,
	CPCAP_AUDIO_OUT_NONE,
	CPCAP_AUDIO_OUT_NONE,
	CPCAP_AUDIO_OUT_NONE,
	CPCAP_AUDIO_OUT_NONE,
	CPCAP_AUDIO_OUT_NONE,
	CPCAP_AUDIO_OUT_NONE,
	CPCAP_AUDIO_BALANCE_NEUTRAL,
	CPCAP_AUDIO_BALANCE_NEUTRAL,
	CPCAP_AUDIO_BALANCE_NEUTRAL,
	0,			/* output gain */
	CPCAP_AUDIO_IN_NONE,
	0,			/* input_gain */
	CPCAP_AUDIO_RAT_NONE
};

/* Define regulator to turn on the audio portion of cpcap */
struct regulator *audio_reg;

static inline int is_mic_stereo(int microphone)
{
	if (microphone == CPCAP_AUDIO_IN_DUAL_INTERNAL
		|| microphone == CPCAP_AUDIO_IN_DUAL_EXTERNAL)
		return 1;
	return 0;
}

static inline int is_codec_changed(struct cpcap_audio_state *state,
					struct cpcap_audio_state *prev_state)
{
	if (state->codec_mode != prev_state->codec_mode ||
		state->codec_rate != prev_state->codec_rate ||
		state->microphone != prev_state->microphone)
		return 1;
	return 0;
}

static inline int is_stdac_changed(struct cpcap_audio_state *state,
					struct cpcap_audio_state *prev_state)
{
	if (state->stdac_mode != prev_state->stdac_mode ||
		state->stdac_rate != prev_state->stdac_rate)
		return 1;
	return 0;
}

static inline int is_mute_changed(struct cpcap_audio_state *state,
				struct cpcap_audio_state *prev_state)
{
	if (state->codec_mute != prev_state->codec_mute ||
		state->stdac_mute != prev_state->stdac_mute ||
		state->analog_source != prev_state->analog_source)
		return 1;
	return 0;
}

static inline int is_output_bt_only(struct cpcap_audio_state *state)
{
	if (state->codec_primary_speaker == CPCAP_AUDIO_OUT_BT_MONO
		&& state->codec_secondary_speaker == CPCAP_AUDIO_OUT_NONE)
		return 1;

	if (state->stdac_primary_speaker == CPCAP_AUDIO_OUT_BT_MONO
	      && state->stdac_secondary_speaker == CPCAP_AUDIO_OUT_NONE)
		return 1;

	if (state->ext_primary_speaker == CPCAP_AUDIO_OUT_BT_MONO
		&& state->ext_secondary_speaker == CPCAP_AUDIO_OUT_NONE)
		return 1;

	return 0;
}

static inline int is_output_headset(struct cpcap_audio_state *state)
{
	if (state->codec_primary_speaker == CPCAP_AUDIO_OUT_STEREO_HEADSET
	|| state->codec_primary_speaker == CPCAP_AUDIO_OUT_MONO_HEADSET
	|| state->codec_secondary_speaker == CPCAP_AUDIO_OUT_STEREO_HEADSET
	|| state->codec_secondary_speaker == CPCAP_AUDIO_OUT_MONO_HEADSET)
		return 1;

	if (state->stdac_primary_speaker == CPCAP_AUDIO_OUT_STEREO_HEADSET
	|| state->stdac_primary_speaker == CPCAP_AUDIO_OUT_MONO_HEADSET
	|| state->stdac_secondary_speaker == CPCAP_AUDIO_OUT_STEREO_HEADSET
	|| state->stdac_secondary_speaker == CPCAP_AUDIO_OUT_MONO_HEADSET)
		return 1;

	if (state->ext_primary_speaker == CPCAP_AUDIO_OUT_STEREO_HEADSET
	|| state->ext_primary_speaker == CPCAP_AUDIO_OUT_MONO_HEADSET
	|| state->ext_secondary_speaker == CPCAP_AUDIO_OUT_STEREO_HEADSET
	|| state->ext_secondary_speaker == CPCAP_AUDIO_OUT_MONO_HEADSET)
		return 1;

	return 0;
}

static inline int is_speaker_turning_off(struct cpcap_audio_state *state,
					struct cpcap_audio_state *prev_state)
{
	if ((prev_state->codec_primary_speaker != CPCAP_AUDIO_OUT_NONE &&
		state->codec_primary_speaker == CPCAP_AUDIO_OUT_NONE) ||
		(prev_state->codec_secondary_speaker != CPCAP_AUDIO_OUT_NONE &&
		state->codec_secondary_speaker == CPCAP_AUDIO_OUT_NONE) ||
		(prev_state->stdac_primary_speaker != CPCAP_AUDIO_OUT_NONE &&
		state->stdac_primary_speaker == CPCAP_AUDIO_OUT_NONE) ||
		(prev_state->stdac_secondary_speaker != CPCAP_AUDIO_OUT_NONE &&
		state->stdac_secondary_speaker == CPCAP_AUDIO_OUT_NONE) ||
		(prev_state->ext_primary_speaker != CPCAP_AUDIO_OUT_NONE &&
		state->ext_primary_speaker == CPCAP_AUDIO_OUT_NONE) ||
		(prev_state->ext_secondary_speaker != CPCAP_AUDIO_OUT_NONE &&
		state->ext_secondary_speaker == CPCAP_AUDIO_OUT_NONE))
		return 1;

	return 0;
}

static inline int is_output_changed(struct cpcap_audio_state *state,
					struct cpcap_audio_state *prev_state)
{
	if (state->codec_primary_speaker != prev_state->codec_primary_speaker
	|| state->codec_primary_balance != prev_state->codec_primary_balance
		|| state->codec_secondary_speaker !=
					prev_state->codec_secondary_speaker)
		return 1;

	if (state->stdac_primary_speaker != prev_state->stdac_primary_speaker
	|| state->stdac_primary_balance != prev_state->stdac_primary_balance
	|| state->stdac_secondary_speaker !=
		prev_state->stdac_secondary_speaker)
		return 1;

	if (state->ext_primary_speaker != prev_state->ext_primary_speaker
	|| state->ext_primary_balance != prev_state->ext_primary_balance
	|| state->ext_secondary_speaker != prev_state->ext_secondary_speaker)
		return 1;

	return 0;
}

static unsigned short int cpcap_audio_get_codec_output_amp_switches(
						int speaker, int balance)
{
	unsigned short int value = 0;

	CPCAP_AUDIO_DEBUG_LOG("%s() called with speaker = %d\n", __func__,
			  speaker);

	switch (speaker) {
	case CPCAP_AUDIO_OUT_HANDSET:
		value = CPCAP_BIT_A1_EAR_CDC_SW;
		break;

	case CPCAP_AUDIO_OUT_LOUDSPEAKER:
		value = CPCAP_BIT_A2_LDSP_L_CDC_SW;
		break;

	case CPCAP_AUDIO_OUT_STEREO_HEADSET:
		if (balance != CPCAP_AUDIO_BALANCE_L_ONLY)
			value = CPCAP_BIT_ARIGHT_HS_CDC_SW;
		if (balance != CPCAP_AUDIO_BALANCE_R_ONLY)
			value |= CPCAP_BIT_ALEFT_HS_CDC_SW;
		break;

	case CPCAP_AUDIO_OUT_MONO_HEADSET:
		value = CPCAP_BIT_ARIGHT_HS_CDC_SW | CPCAP_BIT_ALEFT_HS_CDC_SW;
		break;

	case CPCAP_AUDIO_OUT_LINEOUT:
		value = CPCAP_BIT_A4_LINEOUT_R_CDC_SW |
			CPCAP_BIT_A4_LINEOUT_L_CDC_SW;
		break;

	case CPCAP_AUDIO_OUT_BT_MONO:
		value = 0;
		break;

	default:
		break;
	}

	value |= CPCAP_BIT_PGA_CDC_EN;

	CPCAP_AUDIO_DEBUG_LOG("Exiting %s() with return value = %d\n", __func__,
			  value);
	return value;
}

static unsigned short int cpcap_audio_get_stdac_output_amp_switches(
						int speaker, int balance)
{
	unsigned short int value = 0;

	CPCAP_AUDIO_DEBUG_LOG("%s() called with speaker = %d\n", __func__,
			  speaker);

	switch (speaker) {
	case CPCAP_AUDIO_OUT_HANDSET:
		value = CPCAP_BIT_A1_EAR_DAC_SW;
		break;

	case CPCAP_AUDIO_OUT_STEREO_HEADSET:
		if (balance != CPCAP_AUDIO_BALANCE_R_ONLY)
			value = CPCAP_BIT_ALEFT_HS_DAC_SW;
		if (balance != CPCAP_AUDIO_BALANCE_L_ONLY)
			value |= CPCAP_BIT_ARIGHT_HS_DAC_SW;
		break;

	case CPCAP_AUDIO_OUT_MONO_HEADSET:
		value = CPCAP_BIT_ARIGHT_HS_DAC_SW | CPCAP_BIT_ALEFT_HS_DAC_SW;
		break;

	case CPCAP_AUDIO_OUT_LOUDSPEAKER:
		value = CPCAP_BIT_A2_LDSP_L_DAC_SW | CPCAP_BIT_MONO_DAC0 |
			CPCAP_BIT_MONO_DAC1;
		break;

	case CPCAP_AUDIO_OUT_LINEOUT:
		value = CPCAP_BIT_A4_LINEOUT_R_DAC_SW |
			CPCAP_BIT_A4_LINEOUT_L_DAC_SW;
		break;

	case CPCAP_AUDIO_OUT_BT_MONO:
		value = 0;
		break;

	default:
		break;
	}

	value |= CPCAP_BIT_PGA_DAC_EN;

	CPCAP_AUDIO_DEBUG_LOG("Exiting %s() with return value = %d\n", __func__,
			  value);
	return value;
}

static unsigned short int cpcap_audio_get_ext_output_amp_switches(
						int speaker,
						int balance,
						int source)
{
	unsigned short int value = 0;
	CPCAP_AUDIO_DEBUG_LOG("%s() called with speaker %d\n", __func__,
								speaker);

	switch (speaker) {
	case CPCAP_AUDIO_OUT_HANDSET:
		value |= CPCAP_BIT_A1_EAR_EXT_SW;
		break;

	case CPCAP_AUDIO_OUT_STEREO_HEADSET:
		if (balance != CPCAP_AUDIO_BALANCE_L_ONLY)
			value = CPCAP_BIT_ARIGHT_HS_EXT_SW;
		if (balance != CPCAP_AUDIO_BALANCE_R_ONLY)
			value |= CPCAP_BIT_ALEFT_HS_EXT_SW;
		break;

	case CPCAP_AUDIO_OUT_MONO_HEADSET:
		value = CPCAP_BIT_ARIGHT_HS_EXT_SW | CPCAP_BIT_ALEFT_HS_EXT_SW;
		break;

	case CPCAP_AUDIO_OUT_LOUDSPEAKER:
		value = CPCAP_BIT_A2_LDSP_L_EXT_SW | CPCAP_BIT_MONO_EXT0 |
			CPCAP_BIT_MONO_EXT1;
		break;

	case CPCAP_AUDIO_OUT_LINEOUT:
		value = CPCAP_BIT_A4_LINEOUT_R_EXT_SW |
			CPCAP_BIT_A4_LINEOUT_L_EXT_SW;
		break;

	case CPCAP_AUDIO_OUT_BT_MONO:
		value = 0;
		break;

	default:
		break;
	}

	if (source == CPCAP_AUDIO_ANALOG_SOURCE_L ||
	    source == CPCAP_AUDIO_ANALOG_SOURCE_R) {
		/*we're only using one side of the analog input.
		  Send data to both side */
		value |= CPCAP_BIT_MONO_EXT1;
	}

	value |= CPCAP_BIT_PGA_EXT_R_EN | CPCAP_BIT_PGA_EXT_L_EN;

	CPCAP_AUDIO_DEBUG_LOG("Exiting %s() with return value = %d\n", __func__,
			  value);
	return value;
}

static void cpcap_audio_set_output_amp_switches(struct cpcap_audio_state *state)
{
	static unsigned int 	codec_prev_settings = 0,
				stdac_prev_settings = 0,
				ext_prev_settings = 0;

	struct cpcap_regacc reg_changes;
	unsigned short int value1 = 0, value2 = 0;
	int ret_val;

	if (state->codec_mode != CPCAP_AUDIO_CODEC_OFF) {
		value1 =
		    cpcap_audio_get_codec_output_amp_switches(state->
					codec_primary_speaker,
					state->codec_primary_balance);
		value2 =
		    cpcap_audio_get_codec_output_amp_switches(state->
					codec_secondary_speaker,
					state->codec_primary_balance);

		reg_changes.mask = value1 | value2 | codec_prev_settings;
		reg_changes.value = value1 | value2;
		codec_prev_settings = reg_changes.value;

		CPCAP_AUDIO_DEBUG_LOG("RXCOA_value = %#x, mask = %#x\n",
				  reg_changes.value, reg_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap,
						CPCAP_REG_RXCOA,
						reg_changes.value,
						reg_changes.mask);
		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("POWER_IC_REG_CPCAP_RXCOA "
					      "returned error\n");
	}

	/* Second Stdac switches */
	if (state->stdac_mode != CPCAP_AUDIO_STDAC_OFF) {
		value1 =
		    cpcap_audio_get_stdac_output_amp_switches(state->
					stdac_primary_speaker,
					state->stdac_primary_balance);
		value2 =
		    cpcap_audio_get_stdac_output_amp_switches(state->
					stdac_secondary_speaker,
					state->stdac_primary_balance);

		reg_changes.mask = value1 | value2 | stdac_prev_settings;
		reg_changes.value = value1 | value2;
		stdac_prev_settings = reg_changes.value;

		CPCAP_AUDIO_DEBUG_LOG("RXSDOA_value = %#x, mask = %#x\n",
				  reg_changes.value, reg_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap, CPCAP_REG_RXSDOA,
					reg_changes.value, reg_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP__REG_RXSDOA "
					      "returned error, ret_val = %d\n",
					      ret_val);
	}

	if (state->analog_source != CPCAP_AUDIO_ANALOG_SOURCE_OFF) {
		/* Last External source switches */
		value1 =
		    cpcap_audio_get_ext_output_amp_switches(state->
					ext_primary_speaker,
					state->ext_primary_balance,
					state->analog_source);
		value2 =
		    cpcap_audio_get_ext_output_amp_switches(state->
					ext_secondary_speaker,
					state->ext_primary_balance,
					state->analog_source);

		reg_changes.mask = value1 | value2 | ext_prev_settings;
		reg_changes.value = value1 | value2;
		ext_prev_settings = reg_changes.value;

		CPCAP_AUDIO_DEBUG_LOG("RXEPOA_value = %#x, mask = %#x\n",
				      reg_changes.value, reg_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_RXEPOA,
					reg_changes.value,
					reg_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_RXEPOA "
					      "returned error, ret_val = %d\n",
					      ret_val);
	}
}

static bool cpcap_audio_set_bits_for_speaker(int speaker, int balance,
						unsigned short int *message)
{
	CPCAP_AUDIO_DEBUG_LOG("%s() called with speaker = %d\n", __func__,
			  speaker);

	/* Get the data required to enable each possible path */
	switch (speaker) {
	case CPCAP_AUDIO_OUT_HANDSET:
		(*message) |= CPCAP_BIT_A1_EAR_EN;
		break;

	case CPCAP_AUDIO_OUT_STEREO_HEADSET:
		if (balance != CPCAP_AUDIO_BALANCE_R_ONLY)
			(*message) |= CPCAP_BIT_HS_L_EN;
		if (balance != CPCAP_AUDIO_BALANCE_L_ONLY)
			(*message) |= CPCAP_BIT_HS_R_EN;
		break;

	case CPCAP_AUDIO_OUT_MONO_HEADSET:
		(*message) |= CPCAP_BIT_HS_R_EN | CPCAP_BIT_HS_L_EN;
		break;

	case CPCAP_AUDIO_OUT_LOUDSPEAKER:
		(*message) |= CPCAP_BIT_A2_LDSP_L_EN;
		break;

	case CPCAP_AUDIO_OUT_LINEOUT:
		(*message) |= CPCAP_BIT_A4_LINEOUT_R_EN |
				CPCAP_BIT_A4_LINEOUT_L_EN;
		break;

	case CPCAP_AUDIO_OUT_BT_MONO:
		(*message) |= 0;
		break;

	default:
		break;
	}

	return false; /* There is no external loudspeaker on this product */
}

static void cpcap_audio_configure_aud_mute(struct cpcap_audio_state *state,
				struct cpcap_audio_state *previous_state)
{
	static unsigned int prev_codec_mute_data;
	static unsigned int prev_stdac_mute_data;
	static unsigned int prev_ext_mute_data;
	int ret_val;

	if (is_mute_changed(state, previous_state)) {
		struct cpcap_regacc codec_changes = { 0 };
		struct cpcap_regacc stdac_changes = { 0 };
		struct cpcap_regacc ext_changes = { 0 };

		if (state->codec_mute == CPCAP_AUDIO_CODEC_UNMUTE)
			codec_changes.value |= CPCAP_BIT_CDC_SW;

		codec_changes.mask = codec_changes.value | prev_codec_mute_data;

		prev_codec_mute_data = codec_changes.value;

		CPCAP_AUDIO_DEBUG_LOG("codec_value = %#x, mask = %#x\n",
				  codec_changes.value, codec_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_RXCOA,
					codec_changes.value,
					codec_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_RXCOA "
					      "returned error, ret_val = %d\n",
					      ret_val);

		if (state->stdac_mute == CPCAP_AUDIO_STDAC_UNMUTE)
			stdac_changes.value |= CPCAP_BIT_ST_DAC_SW;

		stdac_changes.mask = stdac_changes.value | prev_stdac_mute_data;

		prev_stdac_mute_data = stdac_changes.value;

		CPCAP_AUDIO_DEBUG_LOG("stdac_value = %#x, mask = %#x\n",
				stdac_changes.value, stdac_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_RXSDOA,
					stdac_changes.value,
					stdac_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_RXSDOA "
					      "returned error, ret_val = %d\n",
					      ret_val);

		if (state->analog_source == CPCAP_AUDIO_ANALOG_SOURCE_STEREO)
			ext_changes.value |=
			    CPCAP_BIT_PGA_IN_R_SW | CPCAP_BIT_PGA_IN_L_SW;
		else if (state->analog_source == CPCAP_AUDIO_ANALOG_SOURCE_L)
			ext_changes.value |= CPCAP_BIT_PGA_IN_L_SW;
		else if (state->analog_source == CPCAP_AUDIO_ANALOG_SOURCE_R)
			ext_changes.value |= CPCAP_BIT_PGA_IN_R_SW;
		ext_changes.mask = ext_changes.value | prev_ext_mute_data;

		prev_ext_mute_data = ext_changes.value;

		CPCAP_AUDIO_DEBUG_LOG("ext_value = %#x, mask = %#x\n",
				ext_changes.value, ext_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_RXEPOA,
					ext_changes.value,
					ext_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_RXEPOA "
					      "returned error, ret_val = %d\n",
					      ret_val);
	}
}

static void cpcap_audio_configure_codec(struct cpcap_audio_state *state,
				struct cpcap_audio_state *previous_state) {
	const unsigned int CODEC_FREQ_MASK = CPCAP_BIT_CDC_CLK0
		| CPCAP_BIT_CDC_CLK1 | CPCAP_BIT_CDC_CLK2;
	const unsigned int CODEC_RESET_FREQ_MASK = CODEC_FREQ_MASK
		| CPCAP_BIT_CDC_CLOCK_TREE_RESET;

	static unsigned int prev_codec_data = 0x0, prev_cdai_data = 0x0;

	if (is_codec_changed(state, previous_state)) {
		unsigned int temp_codec_rate = state->codec_rate;
		struct cpcap_regacc cdai_changes = { 0 };
		struct cpcap_regacc codec_changes = { 0 };
		int codec_freq_config = 0;
		int ret_val;

		if (state->rat_type == CPCAP_AUDIO_RAT_CDMA)
			codec_freq_config = (CPCAP_BIT_CDC_CLK0
					| CPCAP_BIT_CDC_CLK1) ; /* 19.2Mhz */
		else
			codec_freq_config = CPCAP_BIT_CDC_CLK2 ; /* 26Mhz */

		/* If a codec is already in use, reset codec to initial state */
		if (previous_state->codec_mode != CPCAP_AUDIO_CODEC_OFF) {
			codec_changes.mask = prev_codec_data
				| CPCAP_BIT_DF_RESET
				| CPCAP_BIT_CDC_CLOCK_TREE_RESET;

			cpcap_regacc_write(state->cpcap, CPCAP_REG_CC,
				codec_changes.value, codec_changes.mask);

			prev_codec_data = 0;
			previous_state->codec_mode = CPCAP_AUDIO_CODEC_OFF;
		}

		temp_codec_rate &= 0x0000000F;
		temp_codec_rate = temp_codec_rate << 9;

		switch (state->codec_mode) {
		case CPCAP_AUDIO_CODEC_LOOPBACK:
		case CPCAP_AUDIO_CODEC_ON:
			/* Turning on the input HPF */
			codec_changes.value |= CPCAP_BIT_CDC_EN_RX |
						CPCAP_BIT_AUDIHPF_0 |
						CPCAP_BIT_AUDIHPF_1;
			if (state->microphone != CPCAP_AUDIO_IN_AUX_INTERNAL &&
				state->microphone != CPCAP_AUDIO_IN_NONE)
				codec_changes.value |= CPCAP_BIT_MIC1_CDC_EN;

			if (state->microphone == CPCAP_AUDIO_IN_AUX_INTERNAL ||
				is_mic_stereo(state->microphone))
				codec_changes.value |= CPCAP_BIT_MIC2_CDC_EN;

		/* falling through intentionally */
		case CPCAP_AUDIO_CODEC_CLOCK_ONLY:
			codec_changes.value |=
				(codec_freq_config | temp_codec_rate |
				CPCAP_BIT_DF_RESET);
			cdai_changes.value |= CPCAP_BIT_CDC_CLK_EN;
			break;

		case CPCAP_AUDIO_CODEC_OFF:
			cdai_changes.value |= CPCAP_BIT_SMB_CDC;
			break;

		default:
			break;
		}

		/* Multimedia uses CLK_IN0, incall uses CLK_IN1 */
		if (state->rat_type != CPCAP_AUDIO_RAT_NONE)
			cdai_changes.value |= CPCAP_BIT_CLK_IN_SEL;

		/* CDMA sholes is using Normal mode for uplink */
		cdai_changes.value |= CPCAP_BIT_CDC_PLL_SEL | CPCAP_BIT_CLK_INV;

		/* OK, now start paranoid codec sequence */
		/* FIRST, make sure the frequency config is right... */
		ret_val = cpcap_regacc_write(state->cpcap, CPCAP_REG_CC,
			codec_freq_config, CODEC_FREQ_MASK);

		/* Next, write the CDAI if it's changed */
		if (prev_cdai_data != cdai_changes.value) {
			cdai_changes.mask = cdai_changes.value
				| prev_cdai_data;
			prev_cdai_data = cdai_changes.value;

			CPCAP_AUDIO_DEBUG_LOG("cdai_value = %#x, mask = %#x\n",
				cdai_changes.value, cdai_changes.mask);

			ret_val = cpcap_regacc_write(state->cpcap,
				CPCAP_REG_CDI, cdai_changes.value,
				cdai_changes.mask);

			if (ret_val != 0)
				CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_CDI returned "
					"error, ret_val = %d\n", ret_val);

			/* Clock tree change -- reset and wait */
			codec_freq_config |= CPCAP_BIT_CDC_CLOCK_TREE_RESET;
			ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_CC,
					codec_freq_config,
					CODEC_RESET_FREQ_MASK);

			if (ret_val != 0)
				CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_CC returned "
					"error, ret_val = %d\n", ret_val);

			/* Wait for clock tree reset to complete */
			msleep(CLOCK_TREE_RESET_TIME);
		}

		/* Clear old settings */
		codec_changes.mask = codec_changes.value | prev_codec_data;
		prev_codec_data    = codec_changes.value;

		CPCAP_AUDIO_DEBUG_LOG("codec_value = %#x, mask = %#x\n",
				  codec_changes.value, codec_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap, CPCAP_REG_CC,
			codec_changes.value, codec_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_CDI "
				"returned error, ret_val = %d\n", ret_val);
	}
}

static void cpcap_audio_configure_stdac(struct cpcap_audio_state *state,
				struct cpcap_audio_state *previous_state)
{
	static unsigned int prev_stdac_data, prev_sdai_data;
	int ret_val;

	if (is_stdac_changed(state, previous_state)) {
		unsigned int temp_stdac_rate = state->stdac_rate;
		struct cpcap_regacc sdai_changes = { 0 };
		struct cpcap_regacc stdac_changes = { 0 };

		/* We need to turn off stdac before changing its settings */
		if (previous_state->stdac_mode != CPCAP_AUDIO_STDAC_OFF) {
			stdac_changes.mask = prev_stdac_data |
					CPCAP_BIT_DF_RESET_ST_DAC;

			ret_val = cpcap_regacc_write(state->cpcap,
						CPCAP_REG_SDAC,
						stdac_changes.value,
						stdac_changes.mask);

			if (ret_val != 0)
				CPCAP_AUDIO_ERROR_LOG(" CPCAP_REG_SDAC "
					"returned error, ret_val = %d\n",
								ret_val);

			prev_stdac_data = 0;
			previous_state->stdac_mode = CPCAP_AUDIO_STDAC_OFF;
		}

		sdai_changes.value = CPCAP_BIT_ST_DIG_AUD_FS0 |
					CPCAP_BIT_DIG_AUD_IN_ST_DAC |
					CPCAP_BIT_ST_L_TIMESLOT0;

		if (state->rat_type == CPCAP_AUDIO_RAT_3G)
			stdac_changes.value |=
			    CPCAP_BIT_ST_DAC_CLK0 | CPCAP_BIT_DF_RESET_ST_DAC;
		else
			stdac_changes.value |= CPCAP_BIT_ST_DAC_CLK2 |
						CPCAP_BIT_DF_RESET_ST_DAC;

		temp_stdac_rate &= 0x0000000F;
		temp_stdac_rate = temp_stdac_rate << 4;

		switch (state->stdac_mode) {
		case CPCAP_AUDIO_STDAC_ON:
			stdac_changes.value |= CPCAP_BIT_ST_DAC_EN |
						 CPCAP_BIT_ST_CLOCK_TREE_RESET;

		/* falling through intentionally */
		case CPCAP_AUDIO_STDAC_CLOCK_ONLY:
			stdac_changes.value |= temp_stdac_rate;
			sdai_changes.value |= CPCAP_BIT_ST_CLK_EN;
			break;

		case CPCAP_AUDIO_STDAC_OFF:
		default:
			break;
		}

		/* Clear old settings */
		stdac_changes.mask = stdac_changes.value | prev_stdac_data;
		prev_stdac_data = stdac_changes.value;

		CPCAP_AUDIO_DEBUG_LOG("STDAC_value = %#x, mask = %#x\n",
				  stdac_changes.value, stdac_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap, CPCAP_REG_SDAC,
				stdac_changes.value, stdac_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_SDAC "
					      "returned error, ret_val = %d\n",
					      ret_val);

		msleep(CLOCK_TREE_RESET_TIME);

		sdai_changes.mask = sdai_changes.value | prev_sdai_data;
		prev_sdai_data = sdai_changes.value;

		CPCAP_AUDIO_DEBUG_LOG("SDAI_value = %#x, mask = %#x\n",
				  sdai_changes.value, sdai_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_SDACDI,
					sdai_changes.value,
					     sdai_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_SDACDI "
					"returned error, ret_val = %d\n",
					ret_val);
	}
}

static void cpcap_audio_configure_input_gains(
	struct cpcap_audio_state *state,
	struct cpcap_audio_state *previous_state)
{
	int ret_val;

	if (state->input_gain != previous_state->input_gain) {
		struct cpcap_regacc reg_changes = { 0 };
		unsigned int temp_input_gain = state->input_gain & 0x0000001F;

		reg_changes.value |= ((temp_input_gain << 5) | temp_input_gain);

		reg_changes.mask = 0x3FF;

		ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_TXMP,
					reg_changes.value,
					reg_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_TXMP "
					"returned error, ret_val = %d\n",
					ret_val);
	}
}

static void cpcap_audio_configure_output_gains(
	struct cpcap_audio_state *state,
	struct cpcap_audio_state *previous_state)
{
	int ret_val;

	if (state->output_gain != previous_state->output_gain) {
		struct cpcap_regacc reg_changes = { 0 };
		unsigned int temp_output_gain = state->output_gain & 0x0000000F;

		reg_changes.value |=
		    ((temp_output_gain << 2) | (temp_output_gain << 8) |
		     (temp_output_gain << 12));

		reg_changes.mask = 0xFF3C;

		ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_RXVC,
					reg_changes.value,
					reg_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_RXVC "
					"returned error, ret_val = %d\n",
					ret_val);
	}
}

static void cpcap_audio_configure_output(
	struct cpcap_audio_state *state,
	struct cpcap_audio_state *previous_state)
{
	static unsigned int prev_aud_out_data;
	int ret_val;

	if (is_output_changed(previous_state, state)) {
		bool activate_ext_loudspeaker = false;
		struct cpcap_regacc reg_changes = { 0 };

		cpcap_audio_set_output_amp_switches(state);

		activate_ext_loudspeaker = cpcap_audio_set_bits_for_speaker(
						state->codec_primary_speaker,
						 state->codec_primary_balance,
						 &(reg_changes.value));

		activate_ext_loudspeaker = activate_ext_loudspeaker ||
		  cpcap_audio_set_bits_for_speaker(
						state->codec_secondary_speaker,
						 CPCAP_AUDIO_BALANCE_NEUTRAL,
						 &(reg_changes.value));

		activate_ext_loudspeaker =
					cpcap_audio_set_bits_for_speaker(
						state->stdac_primary_speaker,
						 state->stdac_primary_balance,
						 &(reg_changes.value));

		activate_ext_loudspeaker = activate_ext_loudspeaker ||
					cpcap_audio_set_bits_for_speaker(
						state->stdac_secondary_speaker,
						 CPCAP_AUDIO_BALANCE_NEUTRAL,
						 &(reg_changes.value));

		activate_ext_loudspeaker =
		  cpcap_audio_set_bits_for_speaker(
						state->ext_primary_speaker,
						 state->ext_primary_balance,
						 &(reg_changes.value));

		activate_ext_loudspeaker = activate_ext_loudspeaker ||
					cpcap_audio_set_bits_for_speaker(
						state->ext_secondary_speaker,
						 CPCAP_AUDIO_BALANCE_NEUTRAL,
						 &(reg_changes.value));

		reg_changes.mask = reg_changes.value | prev_aud_out_data;

		prev_aud_out_data = reg_changes.value;

		CPCAP_AUDIO_DEBUG_LOG("rx0_value = %#x, rx0_mask = %#x\n",
				  reg_changes.value, reg_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap, CPCAP_REG_RXOA,
				reg_changes.value, reg_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_RXOA "
				"returned error, ret_val = %d\n", ret_val);
	}
}

#define CODEC_LOOPBACK_CHANGED() \
	((state->codec_mode != previous_state->codec_mode) && \
	 (state->codec_mode == CPCAP_AUDIO_CODEC_LOOPBACK || \
	  previous_state->codec_mode == CPCAP_AUDIO_CODEC_LOOPBACK))

static void cpcap_audio_configure_input(
	struct cpcap_audio_state *state,
	struct cpcap_audio_state *previous_state) {
	static unsigned int prev_input_data = 0x0;
	struct cpcap_regacc reg_changes = { 0 };
	int ret_val;

	if (state->microphone != previous_state->microphone ||
		CODEC_LOOPBACK_CHANGED()) {

		if (state->codec_mode == CPCAP_AUDIO_CODEC_LOOPBACK)
			reg_changes.value |= CPCAP_BIT_DLM;

		if (previous_state->microphone
		    == CPCAP_AUDIO_IN_HEADSET) {
			cpcap_regacc_write(state->cpcap, CPCAP_REG_GPIO4,
				     0, CPCAP_BIT_GPIO4DRV);
		}
		switch (state->microphone) {
		case CPCAP_AUDIO_IN_HANDSET:
			reg_changes.value |= CPCAP_BIT_MB_ON1R
				| CPCAP_BIT_MIC1_MUX | CPCAP_BIT_MIC1_PGA_EN;
			break;

		case CPCAP_AUDIO_IN_HEADSET:
			reg_changes.value |= CPCAP_BIT_HS_MIC_MUX
				| CPCAP_BIT_MIC1_PGA_EN;
			cpcap_regacc_write(state->cpcap, CPCAP_REG_GPIO4,
					   CPCAP_BIT_GPIO4DRV,
					   CPCAP_BIT_GPIO4DRV);
			break;

		case CPCAP_AUDIO_IN_EXT_BUS:
			reg_changes.value |=  CPCAP_BIT_EMU_MIC_MUX
				| CPCAP_BIT_MIC1_PGA_EN;
			break;

		case CPCAP_AUDIO_IN_AUX_INTERNAL:
			reg_changes.value |= CPCAP_BIT_MB_ON1L
				| CPCAP_BIT_MIC2_MUX | CPCAP_BIT_MIC2_PGA_EN;
			break;

		case CPCAP_AUDIO_IN_DUAL_INTERNAL:
			reg_changes.value |= CPCAP_BIT_MB_ON1R
				| CPCAP_BIT_MIC1_MUX | CPCAP_BIT_MIC1_PGA_EN
				| CPCAP_BIT_MB_ON1L | CPCAP_BIT_MIC2_MUX
				| CPCAP_BIT_MIC2_PGA_EN;
			break;

		case CPCAP_AUDIO_IN_DUAL_EXTERNAL:
			reg_changes.value |= CPCAP_BIT_RX_R_ENCODE
				| CPCAP_BIT_RX_L_ENCODE;
			break;

		case CPCAP_AUDIO_IN_BT_MONO:
			reg_changes.value = 0;
			break;

		default:
			break;
		}

		reg_changes.mask = reg_changes.value | prev_input_data;
		prev_input_data = reg_changes.value;

		ret_val = cpcap_regacc_write(state->cpcap, CPCAP_REG_TXI,
			reg_changes.value, reg_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_TXI "
				"returned error %d\n", ret_val);
	}
}

static void cpcap_audio_configure_power(
	struct cpcap_audio_state *state,
	struct cpcap_audio_state *previous_state,
	int power)
{
	struct cpcap_regacc reg_changes = {0};
	static int previous_power = -1;
	int ret_val;

	CPCAP_AUDIO_DEBUG_LOG("%s() called with power= %d\n", __func__, power);

	if (power != previous_power) {
		static int regulator_enabled = -1;

		if (IS_ERR(audio_reg)) {
			CPCAP_AUDIO_ERROR_LOG("audio_reg not valid for"
							"regulator setup\n");
			return;
		}

		reg_changes.mask = CPCAP_BIT_AUDIO_LOW_PWR |
					CPCAP_BIT_AUD_LOWPWR_SPEED ;
		reg_changes.value = 0;

		if (SLEEP_ACTIVATE_POWER == 2)
			reg_changes.value |= CPCAP_BIT_AUD_LOWPWR_SPEED;

		if (power) {
			if (regulator_enabled == 0) {
				regulator_enable(audio_reg);
				regulator_enabled = 1;
			}
		} else {
			if (regulator_enabled == 1) {
				reg_changes.value |= CPCAP_BIT_AUDIO_LOW_PWR;
				regulator_disable(audio_reg);
				regulator_enabled = 0;
			}
		}

		ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_VAUDIOC,
					reg_changes.value,
					reg_changes.mask);
		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_RXOA "
				"returned error, ret_val = %d\n", ret_val);

		previous_power = power;

		if (power)
			msleep(SLEEP_ACTIVATE_POWER);
	}

	/* Power supply for headset should be controlled at the beginning
	 * (if enabling) or end (if disabling) of the cpcap_audio setting
	 * sequence */
	if (is_output_changed(previous_state, state)) {
		reg_changes.value = 0;
		reg_changes.mask = CPCAP_BIT_ST_HS_CP_EN;

		if (is_output_headset(state))
			reg_changes.value = reg_changes.mask;

		ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_RXOA,
					reg_changes.value,
					reg_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_RXOA "
				"returned error, ret_val = %d\n", ret_val);

		/* If the output type is BT only then put the IC in
		 * lower power mode */
		reg_changes.mask = CPCAP_BIT_AUDIO_LOW_PWR |
					CPCAP_BIT_AUD_LOWPWR_SPEED;
		reg_changes.value = 0;

		if (is_output_bt_only(state))
			reg_changes.value = reg_changes.mask;

		CPCAP_AUDIO_DEBUG_LOG("512_value = %#x, 512_mask = %#x\n",
					reg_changes.value, reg_changes.mask);

		ret_val = cpcap_regacc_write(state->cpcap,
					CPCAP_REG_VAUDIOC,
					reg_changes.value,
					reg_changes.mask);

		if (ret_val != 0)
			CPCAP_AUDIO_ERROR_LOG("CPCAP_REG_VAUDIOC "
				"returned error, ret_val = %d\n", ret_val);
	}
}

static void cpcap_audio_register_dump(struct cpcap_audio_state *state)
{
	unsigned short reg_val = 0;

	cpcap_regacc_read(state->cpcap, CPCAP_REG_VAUDIOC, &reg_val);
	printk(KERN_INFO "0x200 = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_CC, &reg_val);
	printk(KERN_INFO "0x201 = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_CDI, &reg_val);
	printk(KERN_INFO "0x202 = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_SDAC, &reg_val);
	printk(KERN_INFO "0x203 = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_SDACDI, &reg_val);
	printk(KERN_INFO "0x204 = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_TXI, &reg_val);
	printk(KERN_INFO "0x205 = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_TXMP, &reg_val);
	printk(KERN_INFO "0x206 = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_RXOA, &reg_val);
	printk(KERN_INFO "0x207 = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_RXVC, &reg_val);
	printk(KERN_INFO "0x208 = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_RXCOA, &reg_val);
	printk(KERN_INFO "0x209 = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_RXSDOA, &reg_val);
	printk(KERN_INFO "0x20A = %x\n", reg_val);
	cpcap_regacc_read(state->cpcap, CPCAP_REG_RXEPOA, &reg_val);
	printk(KERN_INFO "0x20B = %x\n", reg_val);
}

void cpcap_audio_set_audio_state(struct cpcap_audio_state *state)
{
	struct cpcap_audio_state *previous_state = &previous_state_struct;

	if (state->codec_mute == CPCAP_AUDIO_CODEC_BYPASS_LOOP)
		state->codec_mode = CPCAP_AUDIO_CODEC_ON;

	if (state->codec_mode == CPCAP_AUDIO_CODEC_OFF ||
	    state->codec_mode == CPCAP_AUDIO_CODEC_CLOCK_ONLY)
		state->codec_mute = CPCAP_AUDIO_CODEC_MUTE;
	else
		state->codec_mute = CPCAP_AUDIO_CODEC_UNMUTE;

	if (state->stdac_mode != CPCAP_AUDIO_STDAC_ON)
		state->stdac_mute = CPCAP_AUDIO_STDAC_MUTE;
	else
		state->stdac_mute = CPCAP_AUDIO_STDAC_UNMUTE;

	if (state->stdac_mode == CPCAP_AUDIO_STDAC_CLOCK_ONLY)
		state->stdac_mode = CPCAP_AUDIO_STDAC_ON;

	if (state->codec_mode != CPCAP_AUDIO_CODEC_OFF ||
	    state->stdac_mode != CPCAP_AUDIO_STDAC_OFF ||
	    state->codec_primary_speaker != CPCAP_AUDIO_OUT_NONE ||
	    state->stdac_primary_speaker != CPCAP_AUDIO_OUT_NONE ||
	    state->ext_primary_speaker != CPCAP_AUDIO_OUT_NONE ||
	    state->microphone != CPCAP_AUDIO_IN_NONE)
		cpcap_audio_configure_power(state, previous_state, 1);

	if (is_speaker_turning_off(previous_state, state))
		cpcap_audio_configure_output(state, previous_state);

	if (is_codec_changed(state, previous_state) ||
		is_stdac_changed(state, previous_state)) {
		int codec_mute = state->codec_mute;
		int stdac_mute = state->stdac_mute;

		state->codec_mute = CPCAP_AUDIO_CODEC_MUTE;
		state->stdac_mute = CPCAP_AUDIO_STDAC_MUTE;

		cpcap_audio_configure_aud_mute(state, previous_state);

		previous_state->codec_mute = state->codec_mute;
		previous_state->stdac_mute = state->stdac_mute;

		state->codec_mute = codec_mute;
		state->stdac_mute = stdac_mute;

		cpcap_audio_configure_codec(state, previous_state);
		cpcap_audio_configure_stdac(state, previous_state);
	}

	cpcap_audio_configure_input(state, previous_state);

	cpcap_audio_configure_input_gains(state, previous_state);

	cpcap_audio_configure_output_gains(state, previous_state);

	msleep(SLEEP_CODEC_STDAC_PGA_SWITCH);

	cpcap_audio_configure_aud_mute(state, previous_state);

	msleep(SLEEP_OUTPUT_AMPS_ENABLE);

	cpcap_audio_configure_output(state, previous_state);

	if (state->codec_mode == CPCAP_AUDIO_CODEC_OFF &&
	    state->stdac_mode == CPCAP_AUDIO_STDAC_OFF &&
	    state->codec_primary_speaker == CPCAP_AUDIO_OUT_NONE &&
	    state->stdac_primary_speaker == CPCAP_AUDIO_OUT_NONE &&
	    state->ext_primary_speaker == CPCAP_AUDIO_OUT_NONE &&
	    state->microphone == CPCAP_AUDIO_IN_NONE)
		cpcap_audio_configure_power(state, previous_state, 0);

	previous_state_struct = *state;
}

void cpcap_audio_init(struct cpcap_audio_state *state)
{
	CPCAP_AUDIO_DEBUG_LOG("%s() called\n", __func__);

	cpcap_regacc_write(state->cpcap, CPCAP_REG_CC, 0, 0xFFFF);
	cpcap_regacc_write(state->cpcap, CPCAP_REG_CDI, 0, 0xBFFF);
	cpcap_regacc_write(state->cpcap, CPCAP_REG_SDAC, 0, 0xFFF);
	cpcap_regacc_write(state->cpcap, CPCAP_REG_SDACDI, 0, 0x3FFF);
	cpcap_regacc_write(state->cpcap, CPCAP_REG_TXI, 0, 0xFDF);
	cpcap_regacc_write(state->cpcap, CPCAP_REG_TXMP, 0, 0xFFF);
	cpcap_regacc_write(state->cpcap, CPCAP_REG_RXOA, 0, 0x3FF);
	/* cpcap_regacc_write(state->cpcap, CPCAP_REG_RXVC, 0, 0xFFF); */
	cpcap_regacc_write(state->cpcap, CPCAP_REG_RXCOA, 0, 0x7FF);
	cpcap_regacc_write(state->cpcap, CPCAP_REG_RXSDOA, 0, 0x1FFF);
	cpcap_regacc_write(state->cpcap, CPCAP_REG_RXEPOA, 0, 0x7FFF);

	cpcap_regacc_write(state->cpcap, CPCAP_REG_GPIO4,
			   CPCAP_BIT_GPIO4DIR, CPCAP_BIT_GPIO4DIR);

	audio_reg = regulator_get(NULL, "vaudio");

	if (IS_ERR(audio_reg))
		CPCAP_AUDIO_ERROR_LOG("could not get regulator for audio\n");
}
