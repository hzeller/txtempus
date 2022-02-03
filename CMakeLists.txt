cmake_minimum_required(VERSION 3.2)
PROJECT(txtempus
        VERSION 1.0.0
        LANGUAGES C CXX)

include(GNUInstallDirs)
set(CMAKE_CXX_STANDARD 11)
ADD_DEFINITIONS(-Wall -O3)

set(CMAKE_INSTALL_PREFIX /usr)

set(TIME_SOURCES 
    src/dcf77-source.cc 
    src/wwvb-source.cc 
    src/jjy-source.cc 
    src/msf-source.cc)

set(SRC_FILES ${TIME_SOURCES} src/txtempus.cc  src/gpio.cc)

add_executable(txtempus ${SRC_FILES})
target_include_directories(txtempus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

install(TARGETS txtempus DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

