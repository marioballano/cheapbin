cmake_minimum_required(VERSION 3.20)
project(cheapbin LANGUAGES C VERSION 1.0.0)

if(NOT APPLE)
    message(FATAL_ERROR "cheapbin requires macOS (uses Core Audio / AudioToolbox). Linux and Windows are not supported.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(cheapbin
    src/main.c
    src/reader.c
    src/analyzer.c
    src/composer.c
    src/synth.c
    src/audio.c
    src/display.c
)

target_compile_options(cheapbin PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
    -O2
)

target_link_libraries(cheapbin PRIVATE
    "-framework AudioToolbox"
    "-framework CoreFoundation"
)
