//
// Created by dyrel on 4/16/2026.
//

#include "SDCard.h"

#include <cstdio>

#include "fatfs.h"
#include "cmsis_os.h"

static FIL         s_file;
static bool        s_open  = false;
static osMutexId_t s_mutex = nullptr;

static const osMutexAttr_t k_sd_mutex_attr = {
    "sdMtx", osMutexPrioInherit, nullptr, 0U
};

int8_t SD_Init(const char* filename)
{
    if (s_open) return 0;

    if (!s_mutex) s_mutex = osMutexNew(&k_sd_mutex_attr);

    // Guard: verify FATFS driver was linked by MX_FATFS_Init (retUSER == 0).
    // Returns -20 (not a valid FRESULT) so it is distinguishable in the debugger.
    extern uint8_t retUSER;
    if (retUSER != 0) return -20;

    // f_mount failure: return -(FRESULT).  FR_NOT_READY=-3, FR_NO_FILESYSTEM=-13, etc.
    // Check sd_init_fail_step (sd_diskio.c) to see exactly where disk_initialize stopped.
    FRESULT res = f_mount(&USERFatFS, USERPath, 1);
    if (res != FR_OK) return -(int8_t)res;

    // f_open failure: offset FRESULT by -100 so mount vs open errors are distinguishable.
    res = f_open(&s_file, filename, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK) return (int8_t)(-100 - (int8_t)res);

    f_lseek(&s_file, f_size(&s_file));   // seek to end for append behaviour
    s_open = true;
    return 0;
}

int8_t SD_LogNewline(const char* msg)
{
    if (!s_open || !s_mutex) return -1;
    if (osMutexAcquire(s_mutex, pdMS_TO_TICKS(50)) != osOK) return -4;
    int8_t ret = 0;
    if (f_printf(&s_file, "%s\n", msg) < 0) ret = -2;
    else if (f_sync(&s_file) != FR_OK)      ret = -3;
    osMutexRelease(s_mutex);
    return ret;
}

int8_t SD_LogInline(const char* msg)
{
    if (!s_open || !s_mutex) return -1;
    if (osMutexAcquire(s_mutex, pdMS_TO_TICKS(50)) != osOK) return -4;
    int8_t ret = 0;
    if (f_printf(&s_file, "%s", msg) < 0) ret = -2;
    else if (f_sync(&s_file) != FR_OK)    ret = -3;
    osMutexRelease(s_mutex);
    return ret;
}

int8_t SD_Write(const char* data, uint32_t len)
{
    if (!s_open || !s_mutex) return -1;
    if (osMutexAcquire(s_mutex, pdMS_TO_TICKS(50)) != osOK) return -4;
    int8_t ret = 0;
    UINT written = 0;
    if (f_write(&s_file, data, len, &written) != FR_OK || written != len) ret = -2;
    osMutexRelease(s_mutex);
    return ret;
}

int8_t SD_Sync()
{
    if (!s_open || !s_mutex) return -1;
    if (osMutexAcquire(s_mutex, pdMS_TO_TICKS(50)) != osOK) return -4;
    int8_t ret = 0;
    if (f_sync(&s_file) != FR_OK) ret = -3;
    osMutexRelease(s_mutex);
    return ret;
}

int8_t SD_LogGPS(double lat, double lon, double alt,
                 uint8_t hour, uint8_t min, uint8_t sec)
{
    if (!s_open || !s_mutex) return -1;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.7f,%.7f,%.2f,%02u,%02u,%02u\n",
             lat, lon, alt, (unsigned)hour, (unsigned)min, (unsigned)sec);
    if (osMutexAcquire(s_mutex, pdMS_TO_TICKS(50)) != osOK) return -4;
    int8_t ret = 0;
    if (f_puts(buf, &s_file) == EOF) ret = -2;
    else if (f_sync(&s_file) != FR_OK) ret = -3;
    osMutexRelease(s_mutex);
    return ret;
}

int8_t SD_LogDyn(const char* msg)
{
    return SD_LogNewline(msg);
}

int8_t SD_Close()
{
    if (!s_open) return 0;
    if (s_mutex) osMutexAcquire(s_mutex, pdMS_TO_TICKS(200));
    f_close(&s_file);
    f_mount(NULL, USERPath, 0);
    s_open = false;
    if (s_mutex) osMutexRelease(s_mutex);
    return 0;
}
