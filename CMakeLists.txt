cmake_minimum_required(VERSION 3.20)
project(Certus VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ============================================================
# Qt 依赖
# ============================================================

find_package(Qt6 REQUIRED COMPONENTS Core Sql Network Widgets)

# ============================================================
# 后端核心（CLI + GUI 共享）
# ============================================================

set(CORE_SOURCES
    src/core/agentmanager.cpp
    src/core/browsermanager.cpp
    src/core/database.cpp
    src/utils/jsonparser.cpp
    src/utils/crypto.cpp
    src/utils/logger.cpp
)

set(CORE_HEADERS
    src/core/agentmanager.h
    src/core/browsermanager.h
    src/core/database.h
    src/utils/jsonparser.h
    src/utils/crypto.h
    src/utils/logger.h
)

set(GUI_SOURCES
    src/mainwindow.cpp
    src/pages/searchpage.cpp
    src/pages/configpage.cpp
    src/pages/monitorpage.cpp
    src/pages/memorypage.cpp
    src/pages/repairpage.cpp
    src/ui/minibarchart.cpp
)

set(GUI_HEADERS
    src/mainwindow.h
    src/pages/searchpage.h
    src/pages/configpage.h
    src/pages/monitorpage.h
    src/pages/memorypage.h
    src/pages/repairpage.h
    src/ui/minibarchart.h
)

# ============================================================
# CLI 可执行文件（certus_backend）
# ============================================================

add_executable(certus_backend src/main.cpp ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(certus_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(certus_backend PRIVATE
    Qt6::Core Qt6::Sql Qt6::Network
)

# ============================================================
# GUI 可执行文件（certus_gui）
# ============================================================

add_executable(certus_gui
    src/main_gui.cpp
    ${CORE_SOURCES} ${CORE_HEADERS}
    ${GUI_SOURCES} ${GUI_HEADERS}
)

target_include_directories(certus_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(certus_gui PRIVATE
    Qt6::Core Qt6::Sql Qt6::Network Qt6::Widgets
)

# ============================================================
# Windows 平台特定
# ============================================================

if(WIN32)
    target_link_libraries(certus_backend PRIVATE Crypt32)
    target_link_libraries(certus_gui PRIVATE Crypt32)

    set_target_properties(certus_backend PROPERTIES WIN32_EXECUTABLE OFF)
    set_target_properties(certus_gui PROPERTIES WIN32_EXECUTABLE ON)
endif()

# ============================================================
# 测试
# ============================================================

option(BUILD_TESTS "构建测试" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(test_jsonparser src/tests/test_jsonparser.cpp src/utils/jsonparser.cpp)
    target_include_directories(test_jsonparser PRIVATE src)
    target_link_libraries(test_jsonparser PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME test_jsonparser COMMAND test_jsonparser)

    add_executable(test_crypto src/tests/test_crypto.cpp src/utils/crypto.cpp)
    target_include_directories(test_crypto PRIVATE src)
    target_link_libraries(test_crypto PRIVATE Qt6::Core Qt6::Test Crypt32)
    add_test(NAME test_crypto COMMAND test_crypto)

    add_executable(test_database src/tests/test_database.cpp src/core/database.cpp src/utils/crypto.cpp)
    target_include_directories(test_database PRIVATE src)
    target_link_libraries(test_database PRIVATE Qt6::Core Qt6::Sql Qt6::Test Crypt32)
    add_test(NAME test_database COMMAND test_database)
endif()
