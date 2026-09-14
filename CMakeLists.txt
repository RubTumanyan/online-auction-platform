cmake_minimum_required(VERSION 3.24)
project(online_auction_platform VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
include(CTest)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(auction_database STATIC src/database/Database.cpp src/security/Crypto.cpp)
target_include_directories(auction_database PUBLIC src)
target_link_libraries(auction_database PUBLIC unofficial::sqlite3::sqlite3 Drogon::Drogon OpenSSL::Crypto)
if(MSVC)
    target_compile_options(auction_database PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(auction_database PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(auction_http OBJECT
    src/controllers/AuthBidController.cpp
    src/controllers/CatalogController.cpp
    src/controllers/HealthController.cpp
    src/controllers/LotWebSocketController.cpp
    src/controllers/ProfileController.cpp
src/realtime/AuctionEvents.cpp
    src/repositories/CatalogRepository.cpp
    src/repositories/ProfileRepository.cpp
    src/repositories/RecommendationRepository.cpp
    src/runtime/RuntimePaths.cpp
    src/services/AuthBidService.cpp
    src/services/AuctionCloseService.cpp
    src/services/CatalogService.cpp
    src/services/EmailDelivery.cpp
    src/services/ProfileService.cpp
    src/services/RecommendationService.cpp
    src/security/HttpSecurity.cpp)
target_include_directories(auction_http PUBLIC src)
target_link_libraries(auction_http PUBLIC Drogon::Drogon auction_database)
if(MSVC)
    target_compile_options(auction_http PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(auction_http PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(auction_server src/main.cpp)
target_link_libraries(auction_server PRIVATE auction_http auction_database)
set_property(TARGET auction_server PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:auction_server>")

# Copy resources when an input changes. The per-configuration stamp avoids recopying
# all 1,000 images on every incremental build while still supporting Visual Studio.
# Track the editable resources. The 1,000 seed images are immutable inputs verified by
# tests; listing every image as a Ninja dependency makes Windows incremental builds slow.
set(AUCTION_RESOURCE_FILES
    "${PROJECT_SOURCE_DIR}/public/index.html"
    "${PROJECT_SOURCE_DIR}/public/lot.html"
    "${PROJECT_SOURCE_DIR}/public/profile.html"
    "${PROJECT_SOURCE_DIR}/public/styles.css"
    "${PROJECT_SOURCE_DIR}/public/catalog.js"
    "${PROJECT_SOURCE_DIR}/public/lot.js"
    "${PROJECT_SOURCE_DIR}/public/profile.js"
    "${PROJECT_SOURCE_DIR}/public/auth.js"
    "${PROJECT_SOURCE_DIR}/database/image_credits.csv"
    "${PROJECT_SOURCE_DIR}/database/image_manifest.json"
    "${PROJECT_SOURCE_DIR}/database/schema.sql"
    "${PROJECT_SOURCE_DIR}/database/migrations/002_auth_bidding.sql"
    "${PROJECT_SOURCE_DIR}/database/migrations/003_auction_lifecycle.sql"
    "${PROJECT_SOURCE_DIR}/database/migrations/004_email_verification.sql"
    "${PROJECT_SOURCE_DIR}/database/migrations/005_user_interactions.sql"
    "${PROJECT_SOURCE_DIR}/database/seed_auctions.json"
    "${PROJECT_SOURCE_DIR}/database/seed_categories.json")
set(AUCTION_RESOURCE_STAMP "${CMAKE_CURRENT_BINARY_DIR}/auction-resources-$<CONFIG>.stamp")
add_custom_command(OUTPUT "${AUCTION_RESOURCE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:auction_server>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/config/config.example.json"
        "$<TARGET_FILE_DIR:auction_server>/config/config.example.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/public" "$<TARGET_FILE_DIR:auction_server>/public"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/database" "$<TARGET_FILE_DIR:auction_server>/database"
    COMMAND ${CMAKE_COMMAND} -E touch "${AUCTION_RESOURCE_STAMP}"
    DEPENDS "${PROJECT_SOURCE_DIR}/config/config.example.json" ${AUCTION_RESOURCE_FILES}
    COMMENT "Copying configuration, database resources and public files beside auction_server"
    VERBATIM)
add_custom_target(auction_resources DEPENDS "${AUCTION_RESOURCE_STAMP}")
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

if(BUILD_TESTING)
    add_executable(auction_database_tests tests/DatabaseTests.cpp)
    target_link_libraries(auction_database_tests PRIVATE auction_http auction_database)
    add_dependencies(auction_database_tests auction_resources)
    add_test(NAME database_layer COMMAND auction_database_tests)
    set_tests_properties(database_layer PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 30)
    add_test(NAME product_images COMMAND auction_database_tests --verify-images)
    set_tests_properties(product_images PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 30)

    add_executable(auction_catalog_tests tests/CatalogHttpTests.cpp)
    target_link_libraries(auction_catalog_tests PRIVATE auction_http auction_database)
    add_dependencies(auction_catalog_tests auction_resources)
    add_test(NAME catalog_http COMMAND auction_catalog_tests)
    set_tests_properties(catalog_http PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 30)

    add_executable(auction_auth_bid_tests tests/AuthBidHttpTests.cpp)
    target_link_libraries(auction_auth_bid_tests PRIVATE auction_http auction_database)
    add_dependencies(auction_auth_bid_tests auction_resources)
    add_test(NAME auth_and_bidding COMMAND auction_auth_bid_tests)
    set_tests_properties(auth_and_bidding PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 60)

    add_executable(auction_email_verification_tests tests/EmailVerificationHttpTests.cpp)
    target_link_libraries(auction_email_verification_tests PRIVATE auction_http auction_database)
    add_dependencies(auction_email_verification_tests auction_resources)
    add_test(NAME email_verification COMMAND auction_email_verification_tests)
    set_tests_properties(email_verification PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 60)

    add_executable(auction_lifecycle_tests tests/AuctionLifecycleTests.cpp)
    target_link_libraries(auction_lifecycle_tests PRIVATE auction_http auction_database)
    add_dependencies(auction_lifecycle_tests auction_resources)
    add_test(NAME auction_lifecycle COMMAND auction_lifecycle_tests)
    set_tests_properties(auction_lifecycle PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 60)

    add_executable(auction_security_tests tests/SecurityHttpTests.cpp)
    target_link_libraries(auction_security_tests PRIVATE auction_http auction_database)
    add_dependencies(auction_security_tests auction_resources)
    add_test(NAME security_headers_and_limits COMMAND auction_security_tests)
    set_tests_properties(security_headers_and_limits PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 60)

    add_executable(auction_recommendation_tests tests/RecommendationTests.cpp)
    target_link_libraries(auction_recommendation_tests PRIVATE auction_http auction_database)
    add_dependencies(auction_recommendation_tests auction_resources)
    add_test(NAME recommendation_engine COMMAND auction_recommendation_tests)
    set_tests_properties(recommendation_engine PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 60)

    add_executable(auction_profile_tests tests/ProfileHttpTests.cpp)
    target_link_libraries(auction_profile_tests PRIVATE auction_http auction_database)
    add_dependencies(auction_profile_tests auction_resources)
    add_test(NAME profile_http COMMAND auction_profile_tests)
    set_tests_properties(profile_http PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 60)

    add_executable(auction_concurrent_bid_tests tests/ConcurrentBidTests.cpp)
    target_link_libraries(auction_concurrent_bid_tests PRIVATE auction_http auction_database)
    add_dependencies(auction_concurrent_bid_tests auction_resources)
    add_test(NAME concurrent_bidding COMMAND auction_concurrent_bid_tests)
    set_tests_properties(concurrent_bidding PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 60)

    add_executable(auction_websocket_tests tests/WebSocketTests.cpp)
    target_link_libraries(auction_websocket_tests PRIVATE auction_http auction_database)
    add_dependencies(auction_websocket_tests auction_resources)
    add_test(NAME websocket_live_updates COMMAND auction_websocket_tests)
    set_tests_properties(websocket_live_updates PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-working-directory" TIMEOUT 60)
endif()
