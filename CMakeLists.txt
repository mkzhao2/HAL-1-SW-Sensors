cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Kingfisher-SW)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C CXX ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME}
        Core/Inc/Servo_Axon_Mini_MKII.h
        Core/Inc/timing.h
        Core/Inc/Math/Math.h
        Core/Inc/TEST_FUNCTIONS.h
        Core/Inc/ism6hg256x_reg.h
        Core/Inc/IMU_ISM6HGx.h
        Core/Inc/Math/Vector3D.h
        Core/Src/Math/Vector3D.cpp
        Core/Src/interrupt_override.cpp
        Core/Inc/Math/Math.h
        Core/Inc/bmi3.h
        Core/Inc/bmi3_defs.h
        Core/Inc/bmi323.h
        Core/Inc/bmi323_defs.h
        Core/Inc/SIMU_BMI323.h
        Core/Inc/SIMU_BMI323.h
        Core/Src/SIMU_BMI323.cpp
        Core/Inc/Math/Vector3TripleModRedundancy.h
        Core/Src/Vector3TripleModRedundancy.cpp
        Core/Inc/IMUs.h
        Core/Src/IMUs.cpp
        Core/Inc/Ebyte_E22_900T22S.h
        Core/Inc/Ebyte_E22_900T22S_defs.h
        Core/Src/Ebyte_E22_900T22S.cpp
        Core/Inc/Telemetry.h
        Core/Src/Telemetry.cpp
        Core/Src/BMP3xx_platform.cpp
        Core/Src/bmp3.c
        Core/Inc/bmp3.h
        Core/Inc/bmp3_defs.h
        Core/Inc/BMP390.h
        Core/Src/BMP390.cpp
        Core/Inc/LIS2MDL.h
        Core/Src/lis2mdl_reg.c
        Core/Src/LIS2MDL.cpp
        Core/Inc/BMI323_Platform.h
        Core/Src/BMI323_Platform.cpp
        Core/Src/ISM6_Platform.cpp
        Core/Inc/ISM6_Platform.h
        Core/Inc/Barometer.h
        Core/Src/Barometer.cpp
        Core/Inc/Math/FloatTripleModRedundancy.h
        Core/Src/FloatTripleModRedundancy.cpp
        Core/Inc/constants.h
        Core/Inc/CTRLS_ControlAlgorithm.h
        Core/Inc/CTRLS_SensorFusion.h
        Core/Inc/CTRLS_Controls.h
        Core/Src/CTRLS_ControlAlgorithm.cpp
        Core/Src/CTRLS_SensorFusion.cpp
        Core/Src/CTRLS_Controls.cpp
        Core/Src/MAXM10S.cpp
        Core/Src/TinyGPSPlus.cpp
        Core/Src/FlightComputer_SENSORS.cpp
        Core/Src/FlightComputer_DATAUPDATE.cpp
        Core/Src/DataFilter.cpp
        attitude_estimator/src/attitude_estimator.h
        attitude_estimator/src/attitude_estimator.cpp
)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
        Core/Src/Servo_Axon_Mini_MKII.cpp
        Core/Src/timing.cpp
        Core/Src/Math/Math.cpp
        Core/Src/TEST_FUNCTIONS.cpp
        Core/Src/main.cpp
        Core/Src/Initial_conditions.cpp
        Core/Src/ism6hg256x_reg.c
        Core/Src/IMU_ISM6HGx.cpp
        Core/Src/bmi3.c
        Core/Src/bmi323.c
        Core/Src/sd_diskio.c
        Core/Src/SDCard.cpp
        Core/Src/AllanTest.cpp
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    attitude_estimator/src
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Allan-deviation test build: runs only the raw-IMU logger (Core/Src/AllanTest.cpp)
# instead of the flight tasks. Build it with the "AllanTest" preset.
option(ALLAN_TEST_MODE "Build the Allan-deviation IMU logger instead of the flight tasks" OFF)
if (ALLAN_TEST_MODE)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ALLAN_TEST_MODE)
endif ()

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
