cmake_minimum_required(VERSION 3.25)

project(OpenBoat
    VERSION 0.1.0
    DESCRIPTION "Modular open-source chartplotter / multifunction display for boats"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

option(BOAT_BUILD_APP   "Build the Qt/QML display application" ON)
option(BOAT_BUILD_TESTS "Build unit tests"                     ON)

# --- Qt-free libraries (portable, unit-testable) ---------------------------
add_subdirectory(src/core)
add_subdirectory(src/hal)
add_subdirectory(src/modules/nmea0183)
add_subdirectory(src/modules/nmea2000)
add_subdirectory(src/modules/nav)
add_subdirectory(src/modules/sim)

# --- Display application (requires Qt 6) -----------------------------------
if(BOAT_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(BOAT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
