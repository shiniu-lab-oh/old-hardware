#include "sdc251_keys.h"

#include <stddef.h>

typedef struct {
    uint8_t byte;
    uint8_t mask;
    sdc251_key_mask_t key;
    const char *name;
} key_mapping_t;

/* Measured single-button press/release data, not inferred matrix positions. */
static const key_mapping_t mapping[SDC251_KEY_COUNT] = {
    {1, 0x10, SDC251_KEY_MENU,      "MENU"},
    {1, 0x08, SDC251_KEY_EXIT,      "EXIT"},
    {1, 0x01, SDC251_KEY_OK,        "OK"},
    {0, 0x02, SDC251_KEY_VOL_MINUS, "VOL-"},
    {0, 0x01, SDC251_KEY_VOL_PLUS,  "VOL+"},
    {0, 0x08, SDC251_KEY_CH_MINUS,  "CH-"},
    {0, 0x10, SDC251_KEY_CH_PLUS,   "CH+"},
};
static const uint8_t known_bits[SDC251_KEYS_RAW_BYTES] = {0x1B, 0x19, 0, 0, 0};

esp_err_t sdc251_keys_decode(const uint8_t raw[SDC251_KEYS_RAW_BYTES],
                            sdc251_key_mask_t *pressed)
{
    if (pressed == NULL) { return ESP_ERR_INVALID_ARG; }
    *pressed = SDC251_KEY_NONE;
    if (raw == NULL) { return ESP_ERR_INVALID_ARG; }
    for (unsigned i = 0; i < SDC251_KEYS_RAW_BYTES; ++i) {
        if ((raw[i] & (uint8_t)~known_bits[i]) != 0U) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    for (unsigned i = 0; i < SDC251_KEY_COUNT; ++i) {
        if ((raw[mapping[i].byte] & mapping[i].mask) != 0U) {
            *pressed |= mapping[i].key;
        }
    }
    return ESP_OK;
}

esp_err_t sdc251_keys_update(sdc251_keys_state_t *state,
                            const uint8_t raw[SDC251_KEYS_RAW_BYTES],
                            sdc251_key_events_t *events)
{
    if (state == NULL || events == NULL) { return ESP_ERR_INVALID_ARG; }
    events->held = state->held;
    events->down = SDC251_KEY_NONE;
    events->up = SDC251_KEY_NONE;
    sdc251_key_mask_t observed;
    const esp_err_t err = sdc251_keys_decode(raw, &observed);
    if (err != ESP_OK) {
        for (unsigned i = 0; i < SDC251_KEY_COUNT; ++i) {
            state->matching[i] = 0;
        }
        state->candidate = state->held;
        return err;
    }
    for (unsigned i = 0; i < SDC251_KEY_COUNT; ++i) {
        const sdc251_key_mask_t key = mapping[i].key;
        if (state->matching[i] == 0U || ((state->candidate ^ observed) & key) != 0U) {
            state->candidate = (sdc251_key_mask_t)((state->candidate & (uint8_t)~key) |
                                                 (observed & key));
            state->matching[i] = 1;
        } else if (state->matching[i] < SDC251_KEYS_STABLE_SAMPLES) {
            ++state->matching[i];
        }
        if (state->matching[i] == SDC251_KEYS_STABLE_SAMPLES &&
            ((state->held ^ observed) & key) != 0U) {
            state->held ^= key;
            if ((state->held & key) != 0U) {
                events->down |= key;
            } else {
                events->up |= key;
            }
        }
    }
    events->held = state->held;
    return ESP_OK;
}

const char *sdc251_key_name(sdc251_key_mask_t key)
{
    for (unsigned i = 0; i < SDC251_KEY_COUNT; ++i) {
        if (mapping[i].key == key) { return mapping[i].name; }
    }
    return "UNKNOWN";
}
