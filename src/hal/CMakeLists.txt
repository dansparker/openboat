add_library(boat_hal
    src/can_bus.cpp
    src/gpio.cpp
    src/serial_port.cpp
    src/socket_can.cpp
    src/tcp_socket.cpp
    src/udp_socket.cpp)
add_library(boat::hal ALIAS boat_hal)

target_include_directories(boat_hal PUBLIC include)
target_compile_features(boat_hal PUBLIC cxx_std_20)
boat_set_warnings(boat_hal)

if(WIN32)
    target_link_libraries(boat_hal PRIVATE ws2_32)
endif()
