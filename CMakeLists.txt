cmake_minimum_required(VERSION 3.20)
project(YachtManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)

add_executable(yachtmanager
    src/main.cpp
    src/mainwindow.cpp
    src/chartwidget.cpp
    src/pages.cpp
    src/aipanel.cpp
)
target_link_libraries(yachtmanager PRIVATE Qt6::Widgets Qt6::Sql)
