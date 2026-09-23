cmake_minimum_required(VERSION 3.10)
project(console_ui C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

add_executable(console_ui src/main.c)

target_link_libraries(console_ui raylib m pthread dl curl cjson)

file(COPY assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

enable_testing()

add_executable(test_parse test_parse.c)
target_link_libraries(test_parse cjson)
add_test(NAME ParseTest COMMAND test_parse)

add_executable(test_cache_dir test_cache_dir.c)
target_link_libraries(test_cache_dir raylib m pthread dl curl cjson)
add_test(NAME CacheDirTest COMMAND test_cache_dir)
