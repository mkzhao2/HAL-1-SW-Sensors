//
// Created by dyrel on 3/6/2026.
//

#ifndef KINGFISHER_SW_IMUS_H
#define KINGFISHER_SW_IMUS_H

#include "SIMU_BMI323.h"
#include "IMU_ISM6HGx.h"
#include "Math/Vector3TripleModRedundancy.h"


struct IMUsStatus {
    int8_t A = 0;
    int8_t B = 0;
    int8_t C = 0;
    int8_t ISM = 0;
};

class   IMUs {
    SIMU_BMI323 BMI323_A;
    SIMU_BMI323 BMI323_B;
    SIMU_BMI323 BMI323_C;
    IMU_ISM6HGx ISM6HGx;
    TMR tmrComp;

    struct {
        BMI_Data BMIA_d;
        BMI_Data BMIB_d;
        BMI_Data BMIC_d;
    } Raw_BMI;

    BMI_Data Voted_BMI;

    IMUsStatus sensorStatus;

    bool TMR_STATE = false;

    ISM_Data Data_ISM;

    uint8_t TMR_Compute();
    uint8_t Normal_Compute();

public:
    IMUs();

    IMUsStatus Init(bool tmr_on, const BMI_Config& cfg = BMI_Config());
    IMUsStatus Update();
    BMI_Data getRawBMI(uint8_t index);
    BMI_RawSample getRawSample(uint8_t index);
    BMI_Data getVotedBMI();
    ISM_Data getRawISM();
};

#endif //KINGFISHER_SW_IMUS_H