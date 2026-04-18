cmake_minimum_required(VERSION 3.20)
project(cheapbin LANGUAGES C VERSION 1.0.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(APPLE)
    set(PLATFORM_AUDIO_SRC src/audio_macos.c)
    set(PLATFORM_LIBS "-framework AudioToolbox" "-framework CoreFoundation")
elseif(UNIX)
    add_compile_definitions(_DEFAULT_SOURCE)
    find_package(ALSA REQUIRED)
    set(PLATFORM_AUDIO_SRC src/audio_linux.c)
    set(PLATFORM_LIBS ${ALSA_LIBRARIES} pthread m)
else()
    message(FATAL_ERROR "Unsupported platform. cheapbin supports macOS and Linux.")
endif()

add_executable(cheapbin
    src/main.c
    src/reader.c
    src/analyzer.c
    src/composer.c
    src/synth.c
    ${PLATFORM_AUDIO_SRC}
    src/display.c
)

target_compile_options(cheapbin PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
    -O2
)

if(UNIX AND NOT APPLE)
    target_include_directories(cheapbin PRIVATE ${ALSA_INCLUDE_DIRS})
endif()

target_link_libraries(cheapbin PRIVATE ${PLATFORM_LIBS})
