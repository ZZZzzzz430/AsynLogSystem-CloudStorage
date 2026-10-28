cmake_minimum_required(VERSION 3.16)
project(StorageMySqlScaffold LANGUAGES CXX)

# 这是服务端独立练习目标，不给 Client 链接 MySQL，不接管原 Linux 服务构建。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 显式传开发 SDK 根目录；不是数据库数据目录，也不是存密码的配置文件。
set(MYSQL_ROOT "" CACHE PATH "MySQL C API development SDK root")
find_path(MYSQL_INCLUDE_DIR mysql.h
    HINTS "${MYSQL_ROOT}/include"
    PATH_SUFFIXES mysql)
if(WIN32)
    # 优先动态库的 import library，避免把 MSVC 静态运行库误混入 MinGW。
    find_library(MYSQL_CLIENT_LIBRARY NAMES libmysql
        HINTS "${MYSQL_ROOT}/lib")
else()
    find_library(MYSQL_CLIENT_LIBRARY NAMES mysqlclient
        HINTS "${MYSQL_ROOT}/lib" "${MYSQL_ROOT}/lib64")
endif()
if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_CLIENT_LIBRARY)
    message(FATAL_ERROR "MySQL C API headers/library not found; set MYSQL_ROOT or MYSQL_INCLUDE_DIR/MYSQL_CLIENT_LIBRARY")
endif()

add_library(storage_mysql_scaffold STATIC
    MySqlConnection.cpp MySqlConnection.hpp DbStatus.hpp
    UserRepository.cpp UserRepository.hpp
    ../auth/AuthService.cpp ../auth/AuthService.hpp)
target_include_directories(storage_mysql_scaffold PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(storage_mysql_scaffold PRIVATE "${MYSQL_INCLUDE_DIR}")
target_link_libraries(storage_mysql_scaffold PUBLIC "${MYSQL_CLIENT_LIBRARY}")
if(WIN32)
    target_link_libraries(storage_mysql_scaffold PUBLIC ws2_32 secur32)
endif()

# Probe 不读取凭据、不调用 mysql_real_connect，不在配置/编译时自动执行。
add_executable(mysql_sdk_probe sdk_probe.cpp)
target_include_directories(mysql_sdk_probe PRIVATE "${MYSQL_INCLUDE_DIR}")
target_link_libraries(mysql_sdk_probe PRIVATE storage_mysql_scaffold)

# 手动运行才会连接，由环境变量提供配置；构建时不读密码、不访问数据库。
add_executable(mysql_connection_probe connection_probe.cpp)
target_include_directories(mysql_connection_probe PRIVATE "${MYSQL_INCLUDE_DIR}")
target_link_libraries(mysql_connection_probe PRIVATE storage_mysql_scaffold)

include(CTest)
if(BUILD_TESTING)
    add_executable(mysql_connection_tests connection_tests.cpp)
    target_include_directories(mysql_connection_tests PRIVATE "${MYSQL_INCLUDE_DIR}")
    target_link_libraries(mysql_connection_tests PRIVATE storage_mysql_scaffold)
    add_test(NAME mysql_configuration_and_state COMMAND mysql_connection_tests)
endif()

# 统一应用源码编码和告警设置，包含新增的验证目标。
set(storage_mysql_targets storage_mysql_scaffold mysql_sdk_probe mysql_connection_probe)
if(BUILD_TESTING)
    list(APPEND storage_mysql_targets mysql_connection_tests)
endif()

foreach(target IN LISTS storage_mysql_targets)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -finput-charset=UTF-8)
    endif()
endforeach()
