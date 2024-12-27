cmake_minimum_required (VERSION 3.15)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project (helloCLAP VERSION 0.0.1 LANGUAGES C CXX)
set (SOURCE_CODE
        src/gui.cpp
        src/plugin.cpp
        src/plugin_entry.cpp)

set (CLAP_WRAPPER_OUTPUT_NAME ${PROJECT_NAME})
option (CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES "Enable automatic downloading of dependencies" TRUE)
option (CLAP_WRAPPER_BUILD_STANDALONE "Build standalone version of the plugin" TRUE)
option (CLAP_WRAPPER_BUILD_AUV2 "Build Audio Unit v2 version of the plugin" OFF)
option (CLAP_WRAPPER_BUILD_VST3 "Build VST3 version of the plugin" TRUE)
option (CLAP_WRAPPER_COPY_AFTER_BUILD "Copy build output to user directory after build" TRUE)

add_subdirectory (libs/clap-wrapper)
add_subdirectory (libs/clap-helpers EXCLUDE_FROM_ALL)
#add_subdirectory (libs/readerwriterqueue EXCLUDE_FROM_ALL)

add_library (${PROJECT_NAME} MODULE ${SOURCE_CODE})
target_link_libraries(${PROJECT_NAME} PRIVATE ${CLAP_SDK_ROOT} clap-helpers)

set_target_properties(${PROJECT_NAME} PROPERTIES
        PREFIX ""
        SUFFIX ".clap"
        RUNTIME_OUTPUT_NAME "helloCLAP"
)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE user32 gdi32)
endif()