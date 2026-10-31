//
// Allan-deviation IMU logger: a test build that replaces all flight tasks.
// Compiled only when ALLAN_TEST_MODE is defined (CMake preset "AllanTest");
// freertos.c then starts AllanTestTask instead of the flight tasks.
//
// Two tasks, so slow SD writes never stall sampling:
//   AllanTestTask   (High)   polls the 3 BMI323s every 1 ms and puts one CSV line
//                            per new sample into a stream buffer.
//   AllanWriterTask (Normal) drains the stream buffer to the card, syncs every 1 s.
//                            The SD driver sleeps (osDelay) while the card is busy,
//                            so the sampling task keeps running during writes.
//
// Output: ALLAN.CSV, appended; every run starts with its own header block.
//   imu,count,t_us,sens_time,gx,gy,gz,ax,ay,az,temp_raw
//   t_us = MCU time of the poll that read the sample, microseconds since boot
//   gx..az, temp_raw are raw LSB counts; the run header has the scale factors.
//   count gaps     = lines dropped because the SD card fell behind
//   sens_time gaps = sensor samples that were never read (polling too slow)
//
// LED: fast blink = waiting for SD card / IMUs, solid = logging,
//      slow blink = SD write error or dropped lines since boot.
//

#ifdef ALLAN_TEST_MODE
#warning "ALLAN TEST BUILD - NOT FOR FLIGHT"

#include <cstdio>

#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "cmsis_os.h"
#include "main.h"
#include "IMUs.h"
#include "SDCard.h"
#include "timing.h"

namespace {

// ── Test configuration ──────────────────────────────────────────────────────
// Finest ranges: smallest LSB, best for a stationary test. The plain numbers go
// into the run header for offline scaling and must match the register codes.
constexpr uint8_t ACC_ODR       = BMI3_ACC_ODR_200HZ;
constexpr uint8_t GYR_ODR       = BMI3_GYR_ODR_200HZ;
constexpr int     ODR_HZ        = 200;
constexpr uint8_t ACC_RANGE     = BMI3_ACC_RANGE_2G;
constexpr int     ACC_RANGE_G   = 2;
constexpr uint8_t GYR_RANGE     = BMI3_GYR_RANGE_125DPS;
constexpr int     GYR_RANGE_DPS = 125;

constexpr uint32_t SYNC_PERIOD_MS = 1000;       // SD_Sync once per second: <= ~1 s lost on power-off
constexpr size_t   STREAM_BYTES   = 16 * 1024;  // ~0.5 s of data: absorbs slow SD writes
constexpr size_t   STREAM_TRIGGER = 2048;       // wake the writer once this much is buffered

IMUs                 s_imu;
StreamBufferHandle_t s_stream       = nullptr;
volatile uint32_t    s_droppedLines = 0;        // written only by AllanTestTask
volatile bool        s_sdError      = false;    // written only by AllanWriterTask

// microseconds since boot. Returned as a 64 bit int, so this should last us about 1k years.
//  
// CYCCNT is number of clock cycles since boot, provided by the MCU. but, this is
// 32 bit, so it overflows after only like 10 seconds
//
// we thus accumulate our own clock cycles since boot (totalCyc), and increase it
// by the change in CYCCNT since the last time nowUs was called
// 
// WARNING: because of this rollover issue, MUST CALL AT LEAST ONCE EVERY 10 SECONDS
// otherwise CYCCNT rolls over twice and then our delta (cyc - lastCyc) we don't know what actually is
uint64_t nowUs()
{
    static uint32_t lastCyc  = 0;
    static uint64_t totalCyc = 0;
    const uint32_t cyc = DWT->CYCCNT;
    totalCyc += (uint32_t)(cyc - lastCyc);   // wrap-safe delta
    lastCyc = cyc;
    return totalCyc / (SystemCoreClock / 1000000U);
}

void blinkWait()   // fast blink while waiting for hardware
{
    HAL_GPIO_TogglePin(USR_LED_GPIO_Port, USR_LED_Pin);
    osDelay(100);
}

bool initImus()
{
    BMI_Config cfg;
    cfg.acc_odr   = ACC_ODR;
    cfg.acc_range = ACC_RANGE;
    cfg.gyr_odr   = GYR_ODR;
    cfg.gyr_range = GYR_RANGE;

    const IMUsStatus st = s_imu.Init(true, cfg);   // true = all three BMI323s
    return st.A == 0 && st.B == 0 && st.C == 0;
}

void writeRunHeader()
{
    char hdr[384];
    snprintf(hdr, sizeof(hdr),
        "\n=== ALLAN RUN START ===\n"
        "# odr_hz=%d acc_range_g=%d gyr_range_dps=%d\n"
        "# acc_g = raw*%d/32768, gyr_dps = raw*%d/32768, temp_c = temp_raw/512+23\n"
        "# t_us: MCU time of the poll that read the sample, microseconds since boot\n"
        "# sens_time: BMI323 timestamp, 39.0625 us per tick, 32-bit (wraps after ~46.6 h)\n"
        "imu,count,t_us,sens_time,gx,gy,gz,ax,ay,az,temp_raw\n",
        ODR_HZ, ACC_RANGE_G, GYR_RANGE_DPS, ACC_RANGE_G, GYR_RANGE_DPS);
    SD_LogInline(hdr);   // written and synced before logging starts
}

// One CSV line for IMU i into the stream buffer. Whole lines only: if the buffer
// is too full the line is dropped, which shows up as a gap in the count column.
void queueSample(uint8_t i, const BMI_RawSample& s, uint64_t tUs)
{
    // newlib-nano's printf has no %llu: print t_us as seconds + 6-digit remainder
    const unsigned long sec = (unsigned long)(tUs / 1000000U);
    const unsigned long rem = (unsigned long)(tUs % 1000000U);

    char line[112];
    const int n = (sec != 0)
        ? snprintf(line, sizeof(line), "%d,%lu,%lu%06lu,%lu,%d,%d,%d,%d,%d,%d,%d\n",
              i, (unsigned long)s.count, sec, rem, (unsigned long)s.sensTime,
              s.gyr[0], s.gyr[1], s.gyr[2], s.acc[0], s.acc[1], s.acc[2], s.temp)
        : snprintf(line, sizeof(line), "%d,%lu,%lu,%lu,%d,%d,%d,%d,%d,%d,%d\n",
              i, (unsigned long)s.count, rem, (unsigned long)s.sensTime,
              s.gyr[0], s.gyr[1], s.gyr[2], s.acc[0], s.acc[1], s.acc[2], s.temp);
    if (n <= 0 || n >= (int)sizeof(line)) return;

    if (xStreamBufferSpacesAvailable(s_stream) >= (size_t)n)
        xStreamBufferSend(s_stream, line, (size_t)n, 0);
    else
        s_droppedLines = s_droppedLines + 1;
}

void AllanWriterTask(void*)
{
    static char chunk[4096];
    uint32_t lastSyncMs = HAL_GetTick();

    for (;;) {
        // Returns at once if data is waiting; if empty, blocks until
        // STREAM_TRIGGER bytes arrive or 100 ms pass (so the sync below still runs).
        const size_t n = xStreamBufferReceive(s_stream, chunk, sizeof(chunk), pdMS_TO_TICKS(100));
        if (n > 0 && SD_Write(chunk, (uint32_t)n) != 0) s_sdError = true;

        if (HAL_GetTick() - lastSyncMs >= SYNC_PERIOD_MS) {
            lastSyncMs = HAL_GetTick();
            if (SD_Sync() != 0) s_sdError = true;

            if (s_sdError || s_droppedLines != 0)
                HAL_GPIO_TogglePin(USR_LED_GPIO_Port, USR_LED_Pin);   // slow blink = problem
        }
    }
}

} // namespace

extern "C" void AllanTestTask(void*)
{
    MICROS_DWT_Timebase_Init();   // BMI323_delay_us() busy-waits on the DWT cycle counter

    while (SD_Init("ALLAN.CSV") != 0) blinkWait();
    while (!initImus())               blinkWait();
    writeRunHeader();

    osThreadAttr_t writerAttr = {};
    writerAttr.name       = "AllanWriter";
    writerAttr.stack_size = 4096;
    writerAttr.priority   = osPriorityNormal;   // below this task: sampling preempts SD writes

    s_stream = xStreamBufferCreate(STREAM_BYTES, STREAM_TRIGGER);
    if (s_stream == nullptr || osThreadNew(AllanWriterTask, nullptr, &writerAttr) == nullptr) {
        for (;;) blinkWait();     // out of FreeRTOS heap (configTOTAL_HEAP_SIZE)
    }

    HAL_GPIO_WritePin(USR_LED_GPIO_Port, USR_LED_Pin, GPIO_PIN_SET);   // solid = logging

    uint32_t lastCount[3] = {};
    for (;;) {
        s_imu.Update();
        const uint64_t tUs = nowUs();                // one timestamp per poll, right after the reads
        for (uint8_t i = 0; i < 3; i++) {
            const BMI_RawSample s = s_imu.getRawSample(i);
            if (s.count == lastCount[i]) continue;   // no new sample since the last poll
            lastCount[i] = s.count;
            queueSample(i, s, tUs);
        }
        osDelay(1);   // 1 kHz poll vs 200 Hz ODR
    }
}

#endif // ALLAN_TEST_MODE
