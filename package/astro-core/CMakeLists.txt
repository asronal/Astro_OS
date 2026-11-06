cmake_minimum_required(VERSION 3.16)
project(astro-core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_path(NOVA_INCLUDE_DIR libnova/libnova.h REQUIRED)
find_library(NOVA_LIBRARY nova REQUIRED)

add_library(astro-core STATIC src/sky.cpp)
target_include_directories(astro-core PUBLIC include ${NOVA_INCLUDE_DIR})
target_link_libraries(astro-core PUBLIC ${NOVA_LIBRARY} m)

add_executable(astro-sky src/astro-sky.cpp)
target_link_libraries(astro-sky PRIVATE astro-core)

install(TARGETS astro-core ARCHIVE DESTINATION lib)
install(TARGETS astro-sky RUNTIME DESTINATION bin)
install(FILES include/astro/sky.h DESTINATION include/astro)
