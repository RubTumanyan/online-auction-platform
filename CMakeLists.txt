cmake_minimum_required(VERSION 3.24)
project(online_auction_platform VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
include(CTest)

add_library(auction_http OBJECT
    src/controllers/HealthController.cpp
    src/runtime/RuntimePaths.cpp)
target_include_directories(auction_http PUBLIC src)
target_link_libraries(auction_http PUBLIC Drogon::Drogon)
if(MSVC)
    target_compile_options(auction_http PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(auction_http PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(auction_server src/main.cpp)
target_link_libraries(auction_server PRIVATE auction_http)
set_property(TARGET auction_server PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:auction_server>")

# Run on every build, including a build of only the IDE startup target.
# TARGET_FILE_DIR handles both Ninja and multi-configuration generators.
# Copy only the public example configuration, never a developer's secrets.
add_custom_target(auction_resources
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:auction_server>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/config/config.example.json"
        "$<TARGET_FILE_DIR:auction_server>/config/config.example.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/public" "$<TARGET_FILE_DIR:auction_server>/public"
    COMMENT "Copying configuration and public files beside auction_server"
    VERBATIM)
add_dependencies(auction_server auction_resources)

if(BUILD_TESTING)
    add_executable(auction_tests tests/HttpTests.cpp)
    target_link_libraries(auction_tests PRIVATE auction_http)
    add_dependencies(auction_tests auction_resources)
    # A directory with no config/ or public/ catches working-directory regressions.
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory")
    add_test(NAME http_smoke COMMAND auction_tests)
    set_tests_properties(http_smoke PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory"
        TIMEOUT 30)
endif()
