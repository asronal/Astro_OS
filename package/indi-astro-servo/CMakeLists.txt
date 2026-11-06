cmake_minimum_required(VERSION 3.16)
project(indi-astro-servo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(INDIDRIVER_LIBRARY indidriver REQUIRED)
find_library(NOVA_LIBRARY nova REQUIRED)
find_path(INDI_INCLUDE_DIR libindi/inditelescope.h REQUIRED)

add_executable(indi_astro_servo astro_servo.cpp)
target_include_directories(indi_astro_servo PRIVATE ${INDI_INCLUDE_DIR}/libindi)
target_link_libraries(indi_astro_servo PRIVATE ${INDIDRIVER_LIBRARY} ${NOVA_LIBRARY})

install(TARGETS indi_astro_servo RUNTIME DESTINATION bin)
