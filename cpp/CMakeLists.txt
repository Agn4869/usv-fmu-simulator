cmake_minimum_required(VERSION 3.20)
project(usv_simulator LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(usv_simulator
    src/main.cpp
    src/fmu_runner.cpp
    src/protocol_codec.cpp
    src/mqtt_bridge.cpp
    src/udp_bridge.cpp
    third_party/mqtt-c/src/mqtt.c
    third_party/mqtt-c/src/mqtt_pal.c
)

target_include_directories(usv_simulator PRIVATE
    src
    third_party/mqtt-c/include
)
target_compile_definitions(usv_simulator PRIVATE WIN32 UNICODE _UNICODE)

if (WIN32 OR MINGW OR MSYS)
    target_link_libraries(usv_simulator PRIVATE kernel32 ws2_32)
endif()

if (MINGW OR MSYS)
    target_link_options(usv_simulator PRIVATE -static -static-libgcc -static-libstdc++)
endif()
