#include "cos_detector.h"
#include <math.h>
#include <string.h>

/* ============================================================================
 * IIR Butterworth Bandpass Filter Coefficients (2nd Order)
 * 
 * Design: Butterworth bandpass 300 Hz - 3000 Hz @ 48 kHz
 * Normalized to prevent overflow in fixed-point arithmetic
 * Generated using standard DSP filter design tools
 * ============================================================================ */

/* Bandpass filter coefficients (normalized by a0 for direct biquad form) */
static const struct {
    float b0, b1, b2;           /* Numerator coefficients */
    float a1, a2;               /* Denominator coefficients (a0=1.0 implicitly) */
} filter_coeffs = {
    .b0 =  0.03571f,            /* Numerator */
    .b1 =  0.0f,
    .b2 = -0.03571f,
    .a1 = -1.78531f,            /* Denominator (a0 normalized to 1.0) */
    .a2 =  0.82498f
};

/* ============================================================================
 * Global Detector Instance
 * ============================================================================ */

cos_detector_t cos_detector = {0};

/* ============================================================================
 * Static Helper Functions
 * ============================================================================ */

/**
 * @brief Apply 2nd order IIR biquad filter to a sample
 */
static float apply_filter(iir_biquad_state_t *state, float sample)
{
    float y = filter_coeffs.b0 * sample +
              filter_coeffs.b1 * state->x1 +
              filter_coeffs.b2 * state->x2 -
              filter_coeffs.a1 * state->y1 -
              filter_coeffs.a2 * state->y2;

    state->x2 = state->x1;
    state->x1 = sample;
    state->y2 = state->y1;
    state->y1 = y;

    return y;
}

/**
 * @brief Convert 16-bit sample to float (-1.0 to +1.0)
 */
static float sample_to_float(int16_t sample)
{
    return (float)sample / 32768.0f;
}

/**
 * @brief Calculate RMS from sum of squares
 */
static float calculate_rms(uint64_t sum_of_squares, uint32_t sample_count)
{
    if (sample_count == 0) {
        return 0.0f;
    }
    return sqrtf((float)sum_of_squares / (float)sample_count);
}

/**
 * @brief Convert RMS linear value to dBFS
 * 
 * dBFS = 20 * log10(RMS / 32768)
 */
static float rms_to_dbfs(float rms_linear)
{
    if (rms_linear < 1e-6f) {
        return -120.0f;  /* Clamp to minimum */
    }
    return 20.0f * log10f(rms_linear / 32768.0f);
}

/**
 * @brief Update thresholds based on noise floor and margins
 */
static void update_thresholds(cos_detector_t *det)
{
    det->threshold_on_dbfs = det->noise_floor_dbfs + (float)det->on_margin_db;
    det->threshold_off_dbfs = det->noise_floor_dbfs + (float)det->off_margin_db;
}

/**
 * @brief State machine transition logic
 */
static void update_state_machine(cos_detector_t *det, uint16_t block_ms)
{
    bool signal_present = det->current_level_dbfs > det->threshold_on_dbfs;
    bool signal_strong = det->current_level_dbfs > det->threshold_off_dbfs;

    det->state_changed = false;

    switch (det->state) {
        case COS_STATE_IDLE:
            if (signal_present) {
                det->state = COS_STATE_ATTACK;
                det->attack_timer_ms = 0;
                det->state_changed = true;
            }
            break;

        case COS_STATE_ATTACK:
            if (signal_present) {
                det->attack_timer_ms += block_ms;
                if (det->attack_timer_ms >= det->attack_ms) {
                    det->state = COS_STATE_COS_ACTIVE;
                    det->new_state_value = 1;
                    det->state_changed = true;
                }
            } else {
                /* Signal lost during attack phase */
                det->state = COS_STATE_IDLE;
                det->state_changed = true;
            }
            break;

        case COS_STATE_COS_ACTIVE:
            if (signal_strong) {
                /* Stay active, keep hang timer reset */
                det->hang_timer_ms = 0;
            } else {
                /* Signal has dropped below OFF threshold */
                det->state = COS_STATE_HANG;
                det->hang_timer_ms = 0;
                det->state_changed = true;
            }
            break;

        case COS_STATE_HANG:
            if (signal_strong) {
                /* Signal recovered, return to active */
                det->state = COS_STATE_COS_ACTIVE;
                det->state_changed = true;
            } else {
                /* Still no signal, increment hang timer */
                det->hang_timer_ms += block_ms;
                if (det->hang_timer_ms >= det->hang_ms) {
                    det->state = COS_STATE_IDLE;
                    det->new_state_value = 0;
                    det->state_changed = true;
                }
            }
            break;

        case COS_STATE_MEASURING:
            /* Noise measurement phase - handled in process_block separately */
            break;

        default:
            break;
    }
}

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

void cos_detector_init(uint8_t on_margin_db, uint8_t off_margin_db,
                       uint16_t attack_ms, uint16_t hang_ms)
{
    /* Clear entire context */
    memset(&cos_detector, 0, sizeof(cos_detector_t));

    /* Initialize configuration */
    cos_detector.on_margin_db = on_margin_db;
    cos_detector.off_margin_db = off_margin_db;
    cos_detector.attack_ms = attack_ms;
    cos_detector.hang_ms = hang_ms;

    /* Start in measuring phase */
    cos_detector.state = COS_STATE_MEASURING;
    cos_detector.phase = COS_PHASE_INIT;
    cos_detector.noise_measure_blocks_done = 0;

    /* Initialize filter state */
    cos_detector.filter_state.x1 = 0.0f;
    cos_detector.filter_state.x2 = 0.0f;
    cos_detector.filter_state.y1 = 0.0f;
    cos_detector.filter_state.y2 = 0.0f;

    /* Initialize accumulators */
    cos_detector.level_acc.sum_of_squares = 0;
    cos_detector.level_acc.sample_count = 0;
    cos_detector.noise_measure_acc.sum_of_squares = 0;
    cos_detector.noise_measure_acc.sample_count = 0;

    /* Set initial output state */
    cos_detector.new_state_value = 0;
}

bool cos_detector_process_sample(int16_t sample)
{
    /* Convert to float */
    float f_sample = sample_to_float(sample);

    /* Apply bandpass filter */
    float filtered = apply_filter(&cos_detector.filter_state, f_sample);

    /* Accumulate for RMS calculation (use absolute value or square) */
    float abs_sample = filtered > 0 ? filtered : -filtered;
    cos_detector.level_acc.sum_of_squares += (uint64_t)(abs_sample * abs_sample * 1e9f);
    cos_detector.level_acc.sample_count++;

    /* During noise measurement phase, also accumulate for noise floor */
    if (cos_detector.phase == COS_PHASE_INIT) {
        cos_detector.noise_measure_acc.sum_of_squares += (uint64_t)(abs_sample * abs_sample * 1e9f);
        cos_detector.noise_measure_acc.sample_count++;
    }

    /* Return true when block is complete (for process_block call) */
    return cos_detector.level_acc.sample_count >= COS_DETECTOR_BLOCK_SAMPLES;
}

bool cos_detector_process_block(uint16_t block_ms)
{
    bool state_changed = false;

    if (cos_detector.level_acc.sample_count == 0) {
        return false;
    }

    /* Calculate RMS for this block */
    float rms_linear = calculate_rms(cos_detector.level_acc.sum_of_squares,
                                      cos_detector.level_acc.sample_count);
    cos_detector.current_level_dbfs = rms_to_dbfs(rms_linear);

    /* Handle measurement phase */
    if (cos_detector.phase == COS_PHASE_INIT) {
        cos_detector.noise_measure_blocks_done++;

        /* Check if measurement is complete */
        if (cos_detector.noise_measure_blocks_done >= COS_DETECTOR_NOISE_MEASURE_BLOCKS) {
            /* Calculate noise floor from accumulated data */
            float noise_rms = calculate_rms(cos_detector.noise_measure_acc.sum_of_squares,
                                           cos_detector.noise_measure_acc.sample_count);
            cos_detector.noise_floor_dbfs = rms_to_dbfs(noise_rms);

            /* Update thresholds based on noise floor */
            update_thresholds(&cos_detector);

            /* Transition to ready phase */
            cos_detector.phase = COS_PHASE_READY;
            cos_detector.state = COS_STATE_IDLE;
            state_changed = true;
        }
    } else {
        /* Normal operation: update state machine */
        update_state_machine(&cos_detector, block_ms);
        state_changed = cos_detector.state_changed;
    }

    /* Reset accumulator for next block */
    cos_detector.level_acc.sum_of_squares = 0;
    cos_detector.level_acc.sample_count = 0;

    return state_changed;
}

uint8_t cos_detector_get_state(void)
{
    return cos_detector.new_state_value;
}

cos_detector_state_t cos_detector_get_sm_state(void)
{
    return cos_detector.state;
}

float cos_detector_get_noise_floor(void)
{
    return cos_detector.noise_floor_dbfs;
}

float cos_detector_get_current_level(void)
{
    return cos_detector.current_level_dbfs;
}

bool cos_detector_is_ready(void)
{
    return cos_detector.phase == COS_PHASE_READY;
}

void cos_detector_update_config(uint8_t on_margin_db, uint8_t off_margin_db,
                                uint16_t attack_ms, uint16_t hang_ms)
{
    cos_detector.on_margin_db = on_margin_db;
    cos_detector.off_margin_db = off_margin_db;
    cos_detector.attack_ms = attack_ms;
    cos_detector.hang_ms = hang_ms;

    /* Recalculate thresholds if noise floor is known */
    if (cos_detector.phase == COS_PHASE_READY) {
        update_thresholds(&cos_detector);
    }
}
