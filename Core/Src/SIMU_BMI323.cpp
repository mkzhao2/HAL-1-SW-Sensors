//
// Created by dyrel on 3/5/2026.
//

#include "SIMU_BMI323.h"

// Full-scale value for a BMI3_ACC_RANGE_* / BMI3_GYR_RANGE_* code; 0 if unknown.
static float accRangeG(uint8_t range)
{
    switch (range) {
        case BMI3_ACC_RANGE_2G:  return BMI3_ACC_RANGE_2G_VAL;
        case BMI3_ACC_RANGE_4G:  return BMI3_ACC_RANGE_4G_VAL;
        case BMI3_ACC_RANGE_8G:  return BMI3_ACC_RANGE_8G_VAL;
        case BMI3_ACC_RANGE_16G: return BMI3_ACC_RANGE_16G_VAL;
        default:                 return 0.0f;
    }
}

static float gyrRangeDps(uint8_t range)
{
    switch (range) {
        case BMI3_GYR_RANGE_125DPS:  return BMI3_GYR_RANGE_125DPS_VAL;
        case BMI3_GYR_RANGE_250DPS:  return BMI3_GYR_RANGE_250DPS_VAL;
        case BMI3_GYR_RANGE_500DPS:  return BMI3_GYR_RANGE_500DPS_VAL;
        case BMI3_GYR_RANGE_1000DPS: return BMI3_GYR_RANGE_1000DPS_VAL;
        case BMI3_GYR_RANGE_2000DPS: return BMI3_GYR_RANGE_2000DPS_VAL;
        default:                     return 0.0f;
    }
}

SIMU_BMI323::SIMU_BMI323()
{
    _sensor_active = false;

    _raw.accel_linear = {0,0,0};
    _raw.ang_vel = {0,0,0};
}

int SIMU_BMI323::Init(BMI_INDEX DeviceNum, const BMI_Config& cfg)
{
    // Scale factors for unit conversion in Update(); reject unsupported ranges
    _accRangeG   = accRangeG(cfg.acc_range);
    _gyrRangeDps = gyrRangeDps(cfg.gyr_range);
    if (_accRangeG == 0.0f || _gyrRangeDps == 0.0f)
        return 6;

    _obj.index = DeviceNum;
    _obj.SPIbus = &hspi4;

    /* Map sensor index to chip select pins */
    switch(_obj.index)
    {
        case SENSOR1_I:
            _obj.cs_port = SIMU_CS1_GPIO_Port;
            _obj.cs_pin  = SIMU_CS1_Pin;
            break;

        case SENSOR2_I:
            _obj.cs_port = SIMU_CS2_GPIO_Port;
            _obj.cs_pin  = SIMU_CS2_Pin;
            break;

        case SENSOR3_I:
            _obj.cs_port = SIMU_CS3_GPIO_Port;
            _obj.cs_pin  = SIMU_CS3_Pin;
            break;

        default:
            return 1;
    }

    device.intf = BMI3_SPI_INTF;
    device.intf_ptr = &_obj;

    device.read = BMI3_SPI_Read;
    device.write = BMI3_SPI_Write;
    device.delay_us = BMI323_delay_us;

    if(bmi323_init(&device) != BMI3_OK)
    {
        return 2;
    }

    config[0].type = BMI323_ACCEL;
    config[1].type = BMI323_GYRO;

    if(bmi323_get_sensor_config(config, 2, &device) != BMI3_OK)
        return 3;

    map_int.acc_drdy_int = BMI3_INT1;
    map_int.gyr_drdy_int = BMI3_INT1;
    map_int.temp_drdy_int = BMI3_INT1;

    if (bmi323_map_interrupt(map_int, &device) != BMI3_OK) {
        return 4;
    }

    // ODR and ranges come from cfg (flight defaults in BMI_Config: 200 Hz, 16 g, 2000 dps).
    // AVG64 on accel: hardware averages 64 internal samples, yielding a
    // noise-reduced output; effective anti-aliasing bandwidth = ODR/4 = 50 Hz at 200 Hz,
    // which keeps the 30 Hz digital cutoff well within the passband.
    // Gyro AVG1: no averaging for lowest latency; hardware BW = ODR/2 = 100 Hz at 200 Hz.
    config[0].cfg.acc.odr      = cfg.acc_odr;
    config[0].cfg.acc.range    = cfg.acc_range;
    config[0].cfg.acc.bwp      = BMI3_ACC_BW_ODR_QUARTER;
    config[0].cfg.acc.avg_num  = BMI3_ACC_AVG64;
    config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

    config[1].cfg.gyr.odr      = cfg.gyr_odr;
    config[1].cfg.gyr.range    = cfg.gyr_range;
    config[1].cfg.gyr.bwp      = BMI3_GYR_BW_ODR_HALF;
    config[1].cfg.gyr.gyr_mode = BMI3_GYR_MODE_NORMAL;
    config[1].cfg.gyr.avg_num  = BMI3_GYR_AVG1;

    if (bmi323_set_sensor_config(config, 2, &device) != BMI3_OK)
        return 5;

    _sensor_active = true;
    return 0;
}

int SIMU_BMI323::Update()
{
    int rslt = BMI323_OK;

    if(!_sensor_active)
    {
        return -999;
    }

    uint16_t int_status = 0;

    rslt = bmi323_get_int1_status(&int_status, &device);
    if (rslt != BMI3_OK)
        return rslt;

    // ODR is 200 Hz (new data every 5 ms); sensor task polls every 2 ms.
    // Return early on calls where data isn't ready yet rather than
    // spinning and blocking the FreeRTOS scheduler.
    // Gate on gyro DRDY only: reading the INT1 status clears it, so also requiring
    // ACC/TEMP DRDY could throw away gyro samples whose flags aren't set together.
    if (!(int_status & BMI3_INT_STATUS_GYR_DRDY))
    {
        return BMI_NO_NEW_DATA;
    }

    bmi3_sensor_data sensor_data[3] = {};
    sensor_data[0].type = BMI323_ACCEL;
    sensor_data[1].type = BMI323_GYRO;
    sensor_data[2].type = BMI323_TEMP;

    rslt = bmi323_get_sensor_data(sensor_data, 3, &device);
    if (rslt != BMI3_OK)
        return rslt;

    _rawSample.acc[0]   = sensor_data[0].sens_data.acc.x;
    _rawSample.acc[1]   = sensor_data[0].sens_data.acc.y;
    _rawSample.acc[2]   = sensor_data[0].sens_data.acc.z;
    _rawSample.gyr[0]   = sensor_data[1].sens_data.gyr.x;
    _rawSample.gyr[1]   = sensor_data[1].sens_data.gyr.y;
    _rawSample.gyr[2]   = sensor_data[1].sens_data.gyr.z;
    _rawSample.temp     = (int16_t)sensor_data[2].sens_data.temp.temp_data;
    _rawSample.sensTime = sensor_data[1].sens_data.gyr.sens_time;
    _rawSample.count++;

    _raw.accel_linear.x = lsb_to_g(sensor_data[0].sens_data.acc.x, _accRangeG, device.resolution);
    _raw.accel_linear.y = lsb_to_g(sensor_data[0].sens_data.acc.y, _accRangeG, device.resolution);
    _raw.accel_linear.z = lsb_to_g(sensor_data[0].sens_data.acc.z, _accRangeG, device.resolution);

    _raw.ang_vel.x = lsb_to_dps(sensor_data[1].sens_data.gyr.x, _gyrRangeDps, device.resolution);
    _raw.ang_vel.y = lsb_to_dps(sensor_data[1].sens_data.gyr.y, _gyrRangeDps, device.resolution);
    _raw.ang_vel.z = lsb_to_dps(sensor_data[1].sens_data.gyr.z, _gyrRangeDps, device.resolution);

    float tempForDBG =
        (float)(((float)((int16_t)sensor_data[2].sens_data.temp.temp_data) / 512.0f) + 23.0f);

    return rslt;
}

// Return the raw data calculated
BMI_Data SIMU_BMI323::getRawData()
{
    return _raw;
}

// Return the latest sample as raw register counts (see BMI_RawSample)
BMI_RawSample SIMU_BMI323::getRawSample()
{
    return _rawSample;
}



// HELPER FUNCS

float SIMU_BMI323::lsb_to_g(int16_t val, float g_range, uint8_t bit_width){
    double power = 2;

    float half_scale = (float)((pow((double)power, (double)bit_width) / 2.0f));

    return (val * g_range) / half_scale;
}

float SIMU_BMI323::lsb_to_dps(int16_t val, float dps, uint8_t bit_width) {
    double power = 2;

    float half_scale = (float)((pow((double)power, (double)bit_width) / 2.0f));

    return (dps / (half_scale)) * (val);
}