//
// Created by dyrel on 3/3/2026.
//

#ifndef KINGFISHER_SW_SIMU_BMI323_H
#define KINGFISHER_SW_SIMU_BMI323_H

#include "bmi323.h"
#include "main.h"
#include "spi.h"
#include "Math/Math.h"
#include "BMI323_Platform.h"

typedef enum {
    SENSOR1_I,
    SENSOR2_I,
    SENSOR3_I
} BMI_INDEX;

struct BMI_Data
{
    Vector3D<float> accel_linear;
    Vector3D<float> ang_vel;
};

// Sensor settings passed to SIMU_BMI323::Init (and IMUs::Init).
// Defaults are the flight settings: 200 Hz matches the CTRLs task rate, and
// 16 g / 2000 dps cover flight dynamics. Finer ranges give a smaller LSB.
struct BMI_Config
{
    uint8_t acc_odr   = BMI3_ACC_ODR_200HZ;
    uint8_t acc_range = BMI3_ACC_RANGE_16G;
    uint8_t gyr_odr   = BMI3_GYR_ODR_200HZ;
    uint8_t gyr_range = BMI3_GYR_RANGE_2000DPS;
};

// One reading straight from the sensor registers, before unit conversion.
struct BMI_RawSample
{
    int16_t  acc[3]   = {}; // LSB counts: g   = count * acc range / 32768
    int16_t  gyr[3]   = {}; // LSB counts: dps = count * gyr range / 32768
    int16_t  temp     = 0;  // deg C = temp / 512 + 23
    uint32_t sensTime = 0;  // BMI323 hardware timestamp, BMI3_SENSORTIME_RESOLUTION (39.0625 us) per tick
    uint32_t count    = 0;  // +1 for every new sample read from the sensor
};

// SIMU_BMI323::Update() return value when the sensor has no new sample yet.
// Positive because it is not an error (Bosch convention: negative = error).
constexpr int BMI_NO_NEW_DATA = 1;

typedef struct {
    SPI_HandleTypeDef* SPIbus;
    BMI_INDEX index;
    GPIO_TypeDef* cs_port;
    uint16_t cs_pin;
} BMIobjPtr;

class SIMU_BMI323
{
public:
    SIMU_BMI323();
    int Init(BMI_INDEX DeviceNum, const BMI_Config& cfg = BMI_Config());
    int Update();   // 0 = new sample, BMI_NO_NEW_DATA = nothing new, < 0 = error
    BMI_Data getRawData();
    BMI_RawSample getRawSample();

private:
    bmi3_dev device;
    bool _sensor_active;
    BMI_INDEX InitDev;
    BMI_Data _raw;
    BMI_RawSample _rawSample;
    BMIobjPtr _obj;

    // Full-scale values matching the configured ranges, used for unit conversion
    float _accRangeG   = BMI3_ACC_RANGE_16G_VAL;
    float _gyrRangeDps = BMI3_GYR_RANGE_2000DPS_VAL;

    bmi3_sens_config config[2];
    bmi3_map_int map_int = {0};

    /*! @brief Converts raw sensor values(LSB) to G value
 *
 *  @param[in] val        : Raw sensor value.
 *  @param[in] g_range    : Accel Range selected (4G).
 *  @param[in] bit_width  : Resolution of the sensor.
 *
 *  @return Accel values in Gravity(G)
 *
 */
    static float lsb_to_g(int16_t val, float g_range, uint8_t bit_width);

    /*!
     * @brief This function converts lsb to degree per second for 16 bit gyro at
     * range 125, 250, 500, 1000 or 2000dps.
     */
    static float lsb_to_dps(int16_t val, float dps, uint8_t bit_width);
};

#endif //KINGFISHER_SW_SIMU_BMI323_H