include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SYSTEM)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(boat_tests
    test_data_bus.cpp
    test_nav_math.cpp
    test_nmea0183.cpp
    test_ais.cpp
    test_nmea2000.cpp
    test_nav.cpp
    test_simulator.cpp
    test_buzzer.cpp
    test_route.cpp
    test_track.cpp)
target_link_libraries(boat_tests PRIVATE
    boat::core boat::hal boat::nmea0183 boat::nmea2000 boat::nav boat::sim boat::buzzer boat::track GTest::gtest_main)
boat_set_warnings(boat_tests)

include(GoogleTest)
gtest_discover_tests(boat_tests)
