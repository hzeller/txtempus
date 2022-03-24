cmake_minimum_required(VERSION 3.3)
PROJECT(txtempus
        VERSION 1.0.0
        LANGUAGES C CXX)

include(GNUInstallDirs)
set(CMAKE_CXX_STANDARD 11)
ADD_DEFINITIONS(-Wall -O3)

set(CMAKE_INSTALL_PREFIX /usr)

set(SRC_FILES 
    src/txtempus.cc
    src/dcf77-source.cc 
    src/wwvb-source.cc 
    src/jjy-source.cc 
    src/msf-source.cc
    src/hardware-control.cc)

set(INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/include)
set(PLATFORM_DEPENDENCIES "")

# select the platform
set(SUPPORTED_PLATFORMS rpi jetson) 
set(PLATFORM "rpi" CACHE STRING "Platform")

if(NOT ${PLATFORM} IN_LIST SUPPORTED_PLATFORMS)
    string(REPLACE ";"  ", " SUPPORTED_PLATFORM_LIST "${SUPPORTED_PLATFORMS}")
    message(FATAL_ERROR "'${PLATFORM}' is not a supported platform.\nSupported platforms: [${SUPPORTED_PLATFORM_LIST}]")
endif() 

message(STATUS "Platform: ${PLATFORM}")

include("cmake/${PLATFORM}-control.cmake")
list(APPEND INCLUDE_DIRS "include/${PLATFORM}")


add_executable(${PROJECT_NAME} ${SRC_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${PLATFORM_DEPENDENCIES})

# install
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

