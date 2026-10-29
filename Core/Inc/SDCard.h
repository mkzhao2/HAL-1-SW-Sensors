//
// Created by dyrel on 4/16/2026.
//

#ifndef KINGFISHER_SW_SDCARD_H
#define KINGFISHER_SW_SDCARD_H

#include <stdint.h>

// Mount the filesystem and open the log file for appending (created if missing).
// Must be called once before any SD_Log* / SD_Write call.
// filename: 8.3 name (8 chars + "." + 3 chars) (long file names are disabled in ffconf.h).
// Only one file can be open; if a file is already open this returns 0 and
// keeps the existing file, even if a different filename is passed.
// Returns:
//    0   success
//  -20   FatFs driver not linked (MX_FATFS_Init() not called before SD_Init())
//   -(FRESULT)   mount failure (e.g. -3 = FR_NOT_READY — check sd_init_fail_step)
//   -(100+FRESULT)  file-open failure after successful mount
int8_t SD_Init(const char* filename = "HAL1_LOG.TXT");

// Append msg + newline, then flush to disk.
int8_t SD_LogNewline(const char* msg);

// Append msg inline (no newline), then flush to disk.
int8_t SD_LogInline(const char* msg);

// Append len bytes of data WITHOUT flushing to disk. Data reaches the card
// only when FatFs's internal sector buffer fills or SD_Sync() is called, so
// anything written since the last sync can be lost on power-off.
// Use for high-rate logging: batch many lines, call SD_Sync() periodically.
// Returns 0 on success, -1 not open, -2 write failed/short (e.g. card full),
// -4 mutex timeout.
int8_t SD_Write(const char* data, uint32_t len);

// Flush everything written so far to the card.
// Returns 0 on success, -1 not open, -3 sync failed, -4 mutex timeout.
int8_t SD_Sync();

// Log one GPS fix as a CSV row: lat,lon,alt,hh,mm,ss
int8_t SD_LogGPS(double lat, double lon, double alt,
                 uint8_t hour, uint8_t min, uint8_t sec);

// Append one dynamics line. Same as SD_LogNewline (goes to the open log file).
int8_t SD_LogDyn(const char* msg);

// Flush, close the log file, and unmount. Call before power-down.
int8_t SD_Close();

#endif //KINGFISHER_SW_SDCARD_H
