cmake_minimum_required(VERSION 3.21)

project(bettercadesimu
    VERSION 0.1.0
    DESCRIPTION "Modern IEC schematic editor and relay-logic simulator"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

add_library(cadcore STATIC
    src/cad/cadfile.cpp
    src/cad/cadfile.h
)
target_link_libraries(cadcore PUBLIC Qt6::Core)
target_include_directories(cadcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

qt_add_executable(bettercadesimu
    src/main.cpp
    src/ui/caditem.cpp
    src/ui/caditem.h
    src/ui/canvasview.cpp
    src/ui/canvasview.h
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/symbols.cpp
    src/ui/symbols.h
)
target_link_libraries(bettercadesimu PRIVATE cadcore Qt6::Widgets)

install(TARGETS bettercadesimu RUNTIME DESTINATION bin)

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_executable(cad_parser_test tests/cad_parser_test.cpp)
    target_link_libraries(cad_parser_test PRIVATE cadcore Qt6::Core)
    add_test(NAME cad_parser_test COMMAND cad_parser_test)
endif()
