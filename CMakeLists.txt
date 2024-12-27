cmake_minimum_required(VERSION 3.14)

project(HelloCLAP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(extern/clap/include)

add_library(HelloCLAP SHARED src/plugin.cpp src/plugin_entry.cpp src/gui.cpp)

set_target_properties(HelloCLAP PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)

if (WIN32)
    target_link_libraries(HelloCLAP PRIVATE user32 gdi32)
endif()