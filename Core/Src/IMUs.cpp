//
// Created by dyrel on 3/6/2026.
//

#include "IMUs.h"

// Update one BMI323 and keep its latest sample in out.
// Returns the IMUsStatus code: 0 = OK (new or unchanged sample), 1 = read error.
static int8_t updateBMI(SIMU_BMI323& bmi, BMI_Data& out)
{
    const int rslt = bmi.Update();
    if (rslt < 0) return 1;                  // read error: keep previous sample
    if (rslt == 0) out = bmi.getRawData();   // new sample (BMI_NO_NEW_DATA: keep previous)
    return 0;
}

//PRIVATE:

uint8_t IMUs::TMR_Compute() {
    Voted_BMI.accel_linear = tmrComp.Vote(Raw_BMI.BMIA_d.accel_linear,
                                          Raw_BMI.BMIB_d.accel_linear,
                                          Raw_BMI.BMIC_d.accel_linear);
    Voted_BMI.ang_vel      = tmrComp.Vote(Raw_BMI.BMIA_d.ang_vel,
                                          Raw_BMI.BMIB_d.ang_vel,
                                          Raw_BMI.BMIC_d.ang_vel);
    return 0;
}
uint8_t IMUs::Normal_Compute() {
    Voted_BMI = BMI323_A.getRawData();
    return 0;
}

//PUBLIC:

IMUs::IMUs() : Data_ISM() {}

IMUsStatus IMUs::Init(bool tmr_on, const BMI_Config& cfg) {
    TMR_STATE = tmr_on;

    sensorStatus.A = BMI323_A.Init(SENSOR1_I, cfg);
    if (TMR_STATE) {
        sensorStatus.B = BMI323_B.Init(SENSOR2_I, cfg);
        sensorStatus.C = BMI323_C.Init(SENSOR3_I, cfg);
    }

    // sensorStatus.ISM = ISM6HGx.Init();  // ISM6HGx not used this flight

    return sensorStatus;
}

IMUsStatus IMUs::Update() {
    sensorStatus.A = updateBMI(BMI323_A, Raw_BMI.BMIA_d);

    if (TMR_STATE) {
        sensorStatus.B = updateBMI(BMI323_B, Raw_BMI.BMIB_d);
        sensorStatus.C = updateBMI(BMI323_C, Raw_BMI.BMIC_d);
    }

    // sensorStatus.ISM = ISM6HGx.Update();  // ISM6HGx not used this flight
    // Data_ISM = ISM6HGx.GetData();

    if (TMR_STATE) TMR_Compute();
    else           Normal_Compute();

    return sensorStatus;
}

BMI_Data IMUs::getVotedBMI() {
    return Voted_BMI;
}

// Index 0-2 for sensors A, B, and C. Sensor A is always on.
BMI_Data IMUs::getRawBMI(uint8_t index) {
    switch (index) {
        case 0:  return Raw_BMI.BMIA_d;
        case 1:  return Raw_BMI.BMIB_d;
        case 2:  return Raw_BMI.BMIC_d;
        default: return {};
    }
}

// Index 0-2 for sensors A, B, and C: latest raw register counts + sample counter.
BMI_RawSample IMUs::getRawSample(uint8_t index) {
    switch (index) {
        case 0:  return BMI323_A.getRawSample();
        case 1:  return BMI323_B.getRawSample();
        case 2:  return BMI323_C.getRawSample();
        default: return {};
    }
}

ISM_Data IMUs::getRawISM() {
    return Data_ISM;
}