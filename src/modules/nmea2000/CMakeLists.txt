add_library(boat_nmea2000
    src/decoder.cpp
    src/source.cpp)
add_library(boat::nmea2000 ALIAS boat_nmea2000)
target_include_directories(boat_nmea2000 PUBLIC include)
target_link_libraries(boat_nmea2000 PUBLIC boat::core boat::hal)
boat_set_warnings(boat_nmea2000)
