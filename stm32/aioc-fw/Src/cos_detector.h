#ifndef COS_DETECTOR_H_
#define COS_DETECTOR_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @file cos_detector.h
 * @brief Software Window Discriminator for Virtual COS (Carrier Detect) Signal
 * 
 * Implements a state-machine based COS detector with:
 * - Bandpass filtering (300-3000 Hz)
 * - RMS level measurement in dBFS
 * - Noise floor estimation
 * - Hysteresis (ON/OFF thresholds)
 * - Attack time (delay before COS activation)
 * - Hang time (persistence after signal disappears)
 */

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define COS_DETECTOR_SAMPLE_RATE        48000U      /**< Audio sample rate in Hz */
#define COS_DETECTOR_BLOCK_SIZE_MS      20U         /**< Audio block duration in ms */
#define COS_DETECTOR_BLOCK_SAMPLES      960U        /**< Samples per block (48000 * 20 / 1000) */

#define COS_DETECTOR_FILTER_LOW_HZ      300U        /**< Bandpass filter lower cutoff */
#define COS_DETECTOR_FILTER_HIGH_HZ     3000U       /**< Bandpass filter upper cutoff */

#define COS_DETECTOR_NOISE_MEASURE_MS   2000U       /**< Noise floor measurement duration */
#define COS_DETECTOR_NOISE_MEASURE_BLOCKS (COS_DETECTOR_NOISE_MEASURE_MS / COS_DETECTOR_BLOCK_SIZE_MS)

/* Default thresholds (dB above noise floor) */
#define COS_DETECTOR_DEFAULT_ON_MARGIN_DB   15      /**< COS ON threshold above noise floor */
#define COS_DETECTOR_DEFAULT_OFF_MARGIN_DB   8      /**< COS OFF threshold above noise floor */

/* Default timing parameters (in milliseconds) */
#define COS_DETECTOR_DEFAULT_ATTACK_MS   80         /**< Minimum audio duration before COS activates */
#define COS_DETECTOR_DEFAULT_HANG_MS     1000       /**< Hang time after signal disappears */

/* ============================================================================
 * Enums and Types
 * ============================================================================ */

/**
 * @brief COS Detector State Machine States
 */
typedef enum {
    COS_STATE_IDLE          = 0,    /**< No activity, waiting for signal */
    COS_STATE_ATTACK        = 1,    /**< Signal detected, counting to attack time */
    COS_STATE_COS_ACTIVE    = 2,    /**< COS is active */
    COS_STATE_HANG          = 3,    /**< Signal lost, but hang timer not expired */
    COS_STATE_MEASURING     = 4     /**< Initial noise floor measurement phase */
} cos_detector_state_t;

/**
 * @brief COS Detector Operational Phase
 */
typedef enum {
    COS_PHASE_INIT          = 0,    /**< Measuring noise floor */
    COS_PHASE_READY         = 1     /**< Normal operation */
} cos_detector_phase_t;

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * @brief IIR Biquad Filter State (2nd order Butterworth)
 */
typedef struct {
    /* State variables for numerator and denominator */
    float x1, x2;                   /**< Input history */
    float y1, y2;                   /**< Output history */
} iir_biquad_state_t;

/**
 * @brief RMS Accumulator for one audio block
 */
typedef struct {
    uint64_t sum_of_squares;        /**< Sum of (sample^2) for RMS calculation */
    uint32_t sample_count;          /**< Number of samples accumulated */
} rms_accumulator_t;

/**
 * @brief COS Detector Context
 */
typedef struct {
    /* State machine */
    cos_detector_state_t state;     /**< Current detector state */
    cos_detector_phase_t phase;     /**< Current operational phase */

    /* Configuration (from settings) */
    uint8_t on_margin_db;           /**< Threshold margin above noise (dB) */
    uint8_t off_margin_db;          /**< Threshold margin above noise (dB) */
    uint16_t attack_ms;             /**< Attack time (ms) */
    uint16_t hang_ms;               /**< Hang time (ms) */

    /* Noise floor measurement */
    float noise_floor_dbfs;         /**< Measured noise floor (dBFS) */
    float threshold_on_dbfs;        /**< Calculated ON threshold (dBFS) */
    float threshold_off_dbfs;       /**< Calculated OFF threshold (dBFS) */
    uint16_t noise_measure_blocks_done;  /**< Blocks processed during measurement phase */
    rms_accumulator_t noise_measure_acc; /**< RMS accumulator for noise measurement */

    /* Bandpass filter (IIR 2nd order Butterworth) */
    iir_biquad_state_t filter_state;    /**< Filter state */

    /* Level measurement */
    rms_accumulator_t level_acc;    /**< RMS accumulator for current block */
    float current_level_dbfs;       /**< Current audio level (dBFS) */

    /* Timers */
    uint16_t attack_timer_ms;       /**< Elapsed time in ATTACK state (ms) */
    uint16_t hang_timer_ms;         /**< Elapsed time in HANG state (ms) */

    /* State change flag */
    bool state_changed;             /**< Set to true when COS state changes */
    uint8_t new_state_value;        /**< New COS state (0=inactive, 1=active) for output */
} cos_detector_t;

/* ============================================================================
 * Global Instance
 * ============================================================================ */

extern cos_detector_t cos_detector;

/* ============================================================================
 * Function Prototypes
 * ============================================================================ */

/**
 * @brief Initialize COS detector
 * 
 * Resets all state, starts noise floor measurement phase.
 * Call once during firmware startup.
 * 
 * @param on_margin_db Threshold margin above noise floor for COS ON (dB)
 * @param off_margin_db Threshold margin above noise floor for COS OFF (dB)
 * @param attack_ms Attack time in milliseconds
 * @param hang_ms Hang time in milliseconds
 */
void cos_detector_init(uint8_t on_margin_db, uint8_t off_margin_db,
                       uint16_t attack_ms, uint16_t hang_ms);

/**
 * @brief Process a single audio sample through the detector
 * 
 * Applies bandpass filter and accumulates RMS statistics.
 * Call once per audio sample in the ADC/microphone interrupt handler.
 * 
 * @param sample Audio sample (signed 16-bit)
 * @return true if this call completed an audio block and state machine should update
 */
bool cos_detector_process_sample(int16_t sample);

/**
 * @brief Process state machine update for current audio block
 * 
 * Must be called after COS_DETECTOR_BLOCK_SAMPLES samples have been processed.
 * Updates timers and state machine transitions based on accumulated RMS level.
 * 
 * @param block_ms Duration of the completed block in milliseconds
 * @return true if COS state changed on this call
 */
bool cos_detector_process_block(uint16_t block_ms);

/**
 * @brief Get current COS state (for output to HID/Serial)
 * 
 * @return 1 if COS should be active, 0 if inactive
 */
uint8_t cos_detector_get_state(void);

/**
 * @brief Get current detector state machine state
 * 
 * Useful for debugging/monitoring.
 * 
 * @return Current state (IDLE, ATTACK, COS_ACTIVE, HANG, or MEASURING)
 */
cos_detector_state_t cos_detector_get_sm_state(void);

/**
 * @brief Get measured noise floor level (dBFS)
 * 
 * @return Noise floor in dBFS, or 0.0f if not yet measured
 */
float cos_detector_get_noise_floor(void);

/**
 * @brief Get current audio level (dBFS)
 * 
 * @return Current RMS level in dBFS
 */
float cos_detector_get_current_level(void);

/**
 * @brief Check if noise floor measurement is complete
 * 
 * @return true if measurement phase is done and system is ready
 */
bool cos_detector_is_ready(void);

/**
 * @brief Force update of configuration from settings
 * 
 * Call this if settings registers are modified at runtime.
 * 
 * @param on_margin_db New ON threshold margin (dB)
 * @param off_margin_db New OFF threshold margin (dB)
 * @param attack_ms New attack time (ms)
 * @param hang_ms New hang time (ms)
 */
void cos_detector_update_config(uint8_t on_margin_db, uint8_t off_margin_db,
                                uint16_t attack_ms, uint16_t hang_ms);

#endif /* COS_DETECTOR_H_ */
