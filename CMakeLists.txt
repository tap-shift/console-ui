cmake_minimum_required(VERSION 3.10)
project(console_ui C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

add_executable(console_ui src/main.c)

target_link_libraries(console_ui raylib m pthread dl)
