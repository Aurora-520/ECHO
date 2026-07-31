#include "competition_storage.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ti_msp_dl_config.h"
#include "vehicle_bringup_config.h"

#define COMPETITION_STORAGE_ADDRESS 0x0001FC00UL
#define COMPETITION_STORAGE_MAGIC   0x43464745UL
#define COMPETITION_STORAGE_LEGACY_VERSION 2U
#define COMPETITION_STORAGE_SINGLE_CALIBRATION_VERSION 3U
#define COMPETITION_REFLECTANCE_PROFILE_COUNT 2U
#define COMPETITION_REFLECTANCE_ADC8_INDEX 0U
#define COMPETITION_REFLECTANCE_I2C6_INDEX 1U

#if ECHO_REFLECTANCE_BACKEND == ECHO_REFLECTANCE_BACKEND_ADC8
#define COMPETITION_ACTIVE_REFLECTANCE_INDEX \
    COMPETITION_REFLECTANCE_ADC8_INDEX
#else
#define COMPETITION_ACTIVE_REFLECTANCE_INDEX \
    COMPETITION_REFLECTANCE_I2C6_INDEX
#endif

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size_bytes;
    uint32_t generation;
    competition_settings_t settings;
    uint32_t crc32;
    uint32_t reserved;
} competition_storage_legacy_record_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size_bytes;
    uint32_t generation;
    competition_settings_t settings;
    competition_reflectance_calibration_t reflectance;
    uint32_t crc32;
    uint32_t reserved[2];
} competition_storage_single_calibration_record_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size_bytes;
    uint32_t generation;
    competition_settings_t settings;
    competition_reflectance_calibration_t
        reflectance[COMPETITION_REFLECTANCE_PROFILE_COUNT];
    uint32_t crc32;
    uint32_t reserved[3];
} competition_storage_record_t;

_Static_assert((sizeof(competition_storage_record_t) % 8U) == 0U,
    "competition record must use complete flash words");
_Static_assert((COMPETITION_STORAGE_ADDRESS % 1024U) == 0U,
    "competition storage must start on a sector boundary");

volatile competition_storage_diagnostics_t g_competition_storage_diag;

static competition_settings_t s_cached_settings;
static competition_reflectance_calibration_t
    s_cached_reflectance[COMPETITION_REFLECTANCE_PROFILE_COUNT];
static bool s_cached_settings_valid;
static bool s_cached_reflectance_valid[COMPETITION_REFLECTANCE_PROFILE_COUNT];

static uint32_t CompetitionStorage_RecordCrc(
    const competition_storage_record_t *record)
{
    return CompetitionStorage_Crc32(record,
        (uint32_t) offsetof(competition_storage_record_t, crc32));
}

static uint32_t CompetitionStorage_LegacyRecordCrc(
    const competition_storage_legacy_record_t *record)
{
    return CompetitionStorage_Crc32(record,
        (uint32_t) offsetof(competition_storage_legacy_record_t, crc32));
}

static uint32_t CompetitionStorage_SingleCalibrationRecordCrc(
    const competition_storage_single_calibration_record_t *record)
{
    return CompetitionStorage_Crc32(record,
        (uint32_t) offsetof(
            competition_storage_single_calibration_record_t, crc32));
}

uint32_t CompetitionStorage_Crc32(const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t index;

    for (index = 0U; index < length; index++) {
        uint8_t bit;

        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; bit++) {
            uint32_t mask = 0U - (crc & 1U);

            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

bool CompetitionStorage_Load(competition_settings_t *settings)
{
    const competition_storage_record_t *record =
        (const competition_storage_record_t *) COMPETITION_STORAGE_ADDRESS;
    const competition_storage_legacy_record_t *legacy =
        (const competition_storage_legacy_record_t *)
            COMPETITION_STORAGE_ADDRESS;
    const competition_storage_single_calibration_record_t *single =
        (const competition_storage_single_calibration_record_t *)
            COMPETITION_STORAGE_ADDRESS;
    uint8_t profile;

    g_competition_storage_diag.load_count++;
    s_cached_settings_valid = false;
    memset(s_cached_reflectance_valid, 0,
        sizeof(s_cached_reflectance_valid));
    memset(&s_cached_settings, 0, sizeof(s_cached_settings));
    memset(&s_cached_reflectance, 0, sizeof(s_cached_reflectance));
    if (settings == NULL || record->magic != COMPETITION_STORAGE_MAGIC) {
        g_competition_storage_diag.loaded_valid = 0U;
        return false;
    }
    if (record->version == COMPETITION_SETTINGS_VERSION &&
        record->size_bytes == sizeof(competition_storage_record_t) &&
        record->crc32 == CompetitionStorage_RecordCrc(record)) {
        *settings = record->settings;
        s_cached_settings = record->settings;
        s_cached_settings_valid = true;
        for (profile = 0U;
            profile < COMPETITION_REFLECTANCE_PROFILE_COUNT;
            profile++) {
            s_cached_reflectance[profile] = record->reflectance[profile];
            s_cached_reflectance_valid[profile] =
                record->reflectance[profile].valid_mask == 0xFFU;
        }
        g_competition_storage_diag.generation = record->generation;
    } else if (single->version ==
            COMPETITION_STORAGE_SINGLE_CALIBRATION_VERSION &&
        single->size_bytes ==
            sizeof(competition_storage_single_calibration_record_t) &&
        single->crc32 ==
            CompetitionStorage_SingleCalibrationRecordCrc(single)) {
        *settings = single->settings;
        s_cached_settings = single->settings;
        s_cached_reflectance[COMPETITION_REFLECTANCE_ADC8_INDEX] =
            single->reflectance;
        s_cached_settings_valid = true;
        s_cached_reflectance_valid[COMPETITION_REFLECTANCE_ADC8_INDEX] =
            single->reflectance.valid_mask == 0xFFU;
        g_competition_storage_diag.generation = single->generation;
    } else if (legacy->version == COMPETITION_STORAGE_LEGACY_VERSION &&
        legacy->size_bytes == sizeof(competition_storage_legacy_record_t) &&
        legacy->crc32 == CompetitionStorage_LegacyRecordCrc(legacy)) {
        *settings = legacy->settings;
        s_cached_settings = legacy->settings;
        s_cached_settings_valid = true;
        g_competition_storage_diag.generation = legacy->generation;
    } else {
        g_competition_storage_diag.loaded_valid = 0U;
        return false;
    }
    g_competition_storage_diag.loaded_valid = 1U;
    return true;
}

void CompetitionStorage_SetSettingsSnapshot(
    const competition_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    s_cached_settings = *settings;
    s_cached_settings_valid = true;
}

static bool CompetitionStorage_SaveRecord(
    const competition_settings_t *settings,
    const competition_reflectance_calibration_t
        reflectance[COMPETITION_REFLECTANCE_PROFILE_COUNT])
{
    competition_storage_record_t record;
    const uint32_t *words;
    uint32_t offset;
    bool ok = true;

    if (settings == NULL || reflectance == NULL) {
        return false;
    }
    memset(&record, 0xFF, sizeof(record));
    record.magic = COMPETITION_STORAGE_MAGIC;
    record.version = COMPETITION_SETTINGS_VERSION;
    record.size_bytes = sizeof(record);
    record.generation = g_competition_storage_diag.generation + 1U;
    record.settings = *settings;
    memcpy(record.reflectance, reflectance, sizeof(record.reflectance));
    record.crc32 = CompetitionStorage_RecordCrc(&record);
    words = (const uint32_t *) &record;

    taskENTER_CRITICAL();
    DL_FlashCTL_executeClearStatus(FLASHCTL);
    DL_FlashCTL_unprotectSector(FLASHCTL, COMPETITION_STORAGE_ADDRESS,
        DL_FLASHCTL_REGION_SELECT_MAIN);
    ok = DL_FlashCTL_eraseMemoryFromRAM(FLASHCTL,
        COMPETITION_STORAGE_ADDRESS, DL_FLASHCTL_COMMAND_SIZE_SECTOR) ==
        DL_FLASHCTL_COMMAND_STATUS_PASSED;
    for (offset = 0U; ok && offset < sizeof(record); offset += 8U) {
        DL_FlashCTL_executeClearStatus(FLASHCTL);
        DL_FlashCTL_unprotectSector(FLASHCTL,
            COMPETITION_STORAGE_ADDRESS, DL_FLASHCTL_REGION_SELECT_MAIN);
        ok = DL_FlashCTL_programMemoryFromRAM64WithECCGenerated(FLASHCTL,
            COMPETITION_STORAGE_ADDRESS + offset, &words[offset / 4U]) ==
            DL_FLASHCTL_COMMAND_STATUS_PASSED;
    }
    DL_FlashCTL_protectSector(FLASHCTL, COMPETITION_STORAGE_ADDRESS,
        DL_FLASHCTL_REGION_SELECT_MAIN);
    taskEXIT_CRITICAL();

    if (ok) {
        const competition_storage_record_t *stored =
            (const competition_storage_record_t *)
                COMPETITION_STORAGE_ADDRESS;

        ok = memcmp(stored, &record, sizeof(record)) == 0;
    }
    g_competition_storage_diag.last_save_ok = ok ? 1U : 0U;
    if (ok) {
        g_competition_storage_diag.save_count++;
        g_competition_storage_diag.generation = record.generation;
    } else {
        g_competition_storage_diag.save_failure_count++;
    }
    return ok;
}

bool CompetitionStorage_Save(const competition_settings_t *settings)
{
    if (settings == NULL) {
        return false;
    }
    CompetitionStorage_SetSettingsSnapshot(settings);
    return CompetitionStorage_SaveRecord(settings, s_cached_reflectance);
}

bool CompetitionStorage_LoadReflectanceCalibration(
    competition_reflectance_calibration_t *calibration)
{
    if (calibration == NULL ||
        !s_cached_reflectance_valid[COMPETITION_ACTIVE_REFLECTANCE_INDEX]) {
        return false;
    }
    *calibration =
        s_cached_reflectance[COMPETITION_ACTIVE_REFLECTANCE_INDEX];
    return true;
}

bool CompetitionStorage_SaveReflectanceCalibration(
    const competition_reflectance_calibration_t *calibration)
{
    competition_reflectance_calibration_t
        reflectance[COMPETITION_REFLECTANCE_PROFILE_COUNT];
    bool saved;

    if (calibration == NULL || calibration->valid_mask != 0xFFU ||
        !s_cached_settings_valid) {
        return false;
    }
    memcpy(reflectance, s_cached_reflectance, sizeof(reflectance));
    reflectance[COMPETITION_ACTIVE_REFLECTANCE_INDEX] = *calibration;
    saved = CompetitionStorage_SaveRecord(&s_cached_settings, reflectance);
    if (saved) {
        memcpy(s_cached_reflectance, reflectance,
            sizeof(s_cached_reflectance));
        s_cached_reflectance_valid[COMPETITION_ACTIVE_REFLECTANCE_INDEX] =
            true;
    }
    return saved;
}
