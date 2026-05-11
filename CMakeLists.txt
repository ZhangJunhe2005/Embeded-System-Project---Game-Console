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
set(CMAKE_PROJECT_NAME Unit_4_1_Pong)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${CMAKE_SOURCE_DIR}/ST7789V2_Driver_STM32L4/Core/Src/LCD.c
    ${CMAKE_SOURCE_DIR}/ST7789V2_Driver_STM32L4/Core/Src/ST7789V2_Driver.c
    ${CMAKE_SOURCE_DIR}/Joystick/Joystick.c
    ${CMAKE_SOURCE_DIR}/shared/InputHandler.c
    ${CMAKE_SOURCE_DIR}/shared/Game2BuzzerShim.c
    ${CMAKE_SOURCE_DIR}/game_1/Game_1.c
    ${CMAKE_SOURCE_DIR}/game_2/assets.c
    ${CMAKE_SOURCE_DIR}/game_2/Game_2.c
    ${CMAKE_SOURCE_DIR}/game_2/music.c
    ${CMAKE_SOURCE_DIR}/game_3/Game_3.c
    ${CMAKE_SOURCE_DIR}/game_3/g3_audio.c
    ${CMAKE_SOURCE_DIR}/Renderer/LayerBackgroundIndexed.c
    ${CMAKE_SOURCE_DIR}/Renderer/GunImagesIndexed.c
    ${CMAKE_SOURCE_DIR}/Renderer/KuwuIndexed.c
    ${CMAKE_SOURCE_DIR}/Renderer/Robot1Indexed.c
    ${CMAKE_SOURCE_DIR}/Renderer/BloodhoundIndexed.c
    ${CMAKE_SOURCE_DIR}/Renderer/ApexMenuThumb.c
    ${CMAKE_SOURCE_DIR}/Audio/Audio.c
    ${CMAKE_SOURCE_DIR}/Audio/WeaponFireSfx8.c

)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ST7789V2_Driver_STM32L4/Core/Inc
    ${CMAKE_SOURCE_DIR}/Joystick
    ${CMAKE_SOURCE_DIR}/Buzzer
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/game_1
    ${CMAKE_SOURCE_DIR}/game_2
    ${CMAKE_SOURCE_DIR}/game_3
    ${CMAKE_SOURCE_DIR}/Renderer
    ${CMAKE_SOURCE_DIR}/Audio
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# Enable floating point printf/scanf support
# Note: This adds ~7-10KB to the binary size (ROM), but the STM32L476 has 1MB flash
# so this is not a concern. The main constraint for the projects is the 128KB RAM.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -u _printf_float
    #-u _scanf_float
)
