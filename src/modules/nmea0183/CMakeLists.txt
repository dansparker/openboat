add_library(boat_nmea0183
    src/ais.cpp
    src/output.cpp
    src/parser.cpp
    src/source.cpp)
add_library(boat::nmea0183 ALIAS boat_nmea0183)
target_include_directories(boat_nmea0183 PUBLIC include)
target_link_libraries(boat_nmea0183 PUBLIC boat::core boat::hal boat::nav)
boat_set_warnings(boat_nmea0183)
