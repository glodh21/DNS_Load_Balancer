cmake_minimum_required(VERSION 3.10)
project(aiori)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost headers only (simpler approach)
find_package(Boost REQUIRED)

# Find ldns using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LDNS REQUIRED ldns)

# Add executable
add_executable(aiori
    src/main/dns-idk.cpp
)

# Compiler options
target_compile_options(aiori PRIVATE -Wall -Wextra -O2)

# Include directories
target_include_directories(aiori PRIVATE ${LDNS_INCLUDE_DIRS})
target_include_directories(aiori PRIVATE ${Boost_INCLUDE_DIRS})

# Link libraries - manually specify boost libraries
target_link_libraries(aiori 
    ${LDNS_LIBRARIES}
    boost_system
    pthread
)

# Add linker flags if needed
target_link_options(aiori PRIVATE ${LDNS_LDFLAGS})