
cmake_minimum_required(VERSION 3.16)

project(Client VERSION 0.1 LANGUAGES CXX)

# 设置 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 自动处理 UI、信号槽和资源文件
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 只查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS
    Widgets
    Network
)

# 项目源文件
set(PROJECT_SOURCES
    main.cpp
    # 登录窗口用代码搭建界面；Q_OBJECT 所在头文件交给 AUTOMOC 生成元对象。
    loginwindow.cpp
    loginwindow.h
    # 共用配色与 QPainter 图标，不新增资源包或 Qt 模块依赖。
    clienttheme.cpp
    clienttheme.h
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    ApiClient.hpp
    ApiClient.cpp
)

# 创建 Qt6 可执行程序
qt_add_executable(Client
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

# 链接 Qt6 模块
target_link_libraries(Client PRIVATE
    Qt6::Widgets
    Qt6::Network
)

# 设置可执行文件属性
set_target_properties(Client PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

# 安装配置
include(GNUInstallDirs)

install(TARGETS Client
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 完成 Qt6 可执行文件配置
qt_finalize_executable(Client)

include(CTest)
if(BUILD_TESTING)
    # Manual real-network test; requires isolated credentials and a running server.
    qt_add_executable(client_auth_smoke tests/auth_smoke.cpp ApiClient.cpp ApiClient.hpp)
    target_include_directories(client_auth_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(client_auth_smoke PRIVATE Qt6::Network)
endif()
if(BUILD_TESTING)
    set(CLIENT_UI_TEST_SOURCES ${PROJECT_SOURCES})
    list(REMOVE_ITEM CLIENT_UI_TEST_SOURCES main.cpp)
    qt_add_executable(client_ui_smoke tests/login_ui_smoke.cpp ${CLIENT_UI_TEST_SOURCES})
    target_include_directories(client_ui_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(client_ui_smoke PRIVATE Qt6::Widgets Qt6::Network)
endif()
