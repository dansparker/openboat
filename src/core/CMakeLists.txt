add_library(boat_core
    src/daily_log.cpp
    src/data_bus.cpp
    src/nav_math.cpp)
add_library(boat::core ALIAS boat_core)

target_include_directories(boat_core PUBLIC include)
target_compile_features(boat_core PUBLIC cxx_std_20)
boat_set_warnings(boat_core)

find_package(Threads REQUIRED)
target_link_libraries(boat_core PUBLIC Threads::Threads)
