cmake_minimum_required(VERSION 3.10)
project(aiori)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost headers only (simpler approach)
find_package(Boost REQUIRED)

# Find ldns using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LDNS REQUIRED ldns)

# Find libcurl for health checking
pkg_check_modules(CURL REQUIRED libcurl)

# Add executable for standalone DNS server
add_executable(aiori
    src/main/dns-idk.cpp
    src/config/config_loader.cpp
    src/config/health_checker.cpp
    src/config/load_balancer.cpp
)

# Add executable for PowerDNS backend
add_executable(pdns-backend
    src/main/powerdns_main.cpp
    src/config/config_loader.cpp
    src/config/health_checker.cpp
    src/config/load_balancer.cpp
    src/config/powerdns_backend.cpp
)

# Add executable for DNS Load Balancer with dnsdist algorithms
add_executable(aiori-dnsdist
    src/main/main_dnsdist_lb.cpp
    src/config/config_loader.cpp
    src/config/health_checker.cpp
    load_balancing/dnsdist-lbpolicies.cc
)

# Compiler options for aiori
target_compile_options(aiori PRIVATE -Wall -Wextra -O2)

# Include directories for aiori
target_include_directories(aiori PRIVATE ${LDNS_INCLUDE_DIRS})
target_include_directories(aiori PRIVATE ${Boost_INCLUDE_DIRS})
target_include_directories(aiori PRIVATE ${CURL_INCLUDE_DIRS})

# Link libraries for aiori
target_link_libraries(aiori 
    ${LDNS_LIBRARIES}
    ${CURL_LIBRARIES}
    boost_system
    pthread
)

# Add linker flags for aiori
target_link_options(aiori PRIVATE ${LDNS_LDFLAGS})

# Compiler options for pdns-backend
target_compile_options(pdns-backend PRIVATE -Wall -Wextra -O2)

# Include directories for pdns-backend
target_include_directories(pdns-backend PRIVATE ${CURL_INCLUDE_DIRS})

# Link libraries for pdns-backend
target_link_libraries(pdns-backend 
    ${CURL_LIBRARIES}
    pthread
)

# Compiler options for aiori-dnsdist
target_compile_options(aiori-dnsdist PRIVATE -Wall -Wextra -O2)

# Include directories for aiori-dnsdist
target_include_directories(aiori-dnsdist PRIVATE ${LDNS_INCLUDE_DIRS})
target_include_directories(aiori-dnsdist PRIVATE ${Boost_INCLUDE_DIRS})
target_include_directories(aiori-dnsdist PRIVATE ${CURL_INCLUDE_DIRS})
target_include_directories(aiori-dnsdist PRIVATE ${CMAKE_SOURCE_DIR}/load_balancing)

# Link libraries for aiori-dnsdist
target_link_libraries(aiori-dnsdist 
    ${LDNS_LIBRARIES}
    ${CURL_LIBRARIES}
    boost_system
    pthread
)

# Add linker flags for aiori-dnsdist
target_link_options(aiori-dnsdist PRIVATE ${LDNS_LDFLAGS})