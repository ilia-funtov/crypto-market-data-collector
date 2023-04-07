cmake_minimum_required(VERSION 3.0.0)
project(market-data-collector)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND SRCFILES src/market_data_collector.cpp)

add_executable(market-data-collector ${SRCFILES})

set_target_properties(market-data-collector PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_include_directories(market-data-collector PRIVATE include)
target_include_directories(market-data-collector SYSTEM PRIVATE dependencies)

if (MSVC)
    target_compile_options(market-data-collector PRIVATE /W4)
else()
    target_compile_options(market-data-collector PRIVATE -Wall)
endif()

find_package(Boost REQUIRED program_options log log_setup)
if(Boost_FOUND)
    target_include_directories(market-data-collector SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(market-data-collector PRIVATE ${Boost_LIBRARIES})
else()
    message(FATAL_ERROR "Required boost libraries are not found")
endif()

find_package(OpenSSL REQUIRED)
if(OpenSSL_FOUND)
    target_include_directories(market-data-collector SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(market-data-collector PRIVATE ${OPENSSL_LIBRARIES})
else()
    message(FATAL_ERROR "Openssl libraries are not found")
endif()

find_package(CURL REQUIRED)
if(CURL_FOUND)
    target_include_directories(market-data-collector SYSTEM PRIVATE ${CURL_INCLUDE_DIR})
    target_link_libraries(market-data-collector PRIVATE ${CURL_LIBRARIES})
else()
    message(FATAL_ERROR "Curl library is not found")
endif()

find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if ((DEFINED CLANG_TIDY_EXE) AND (EXISTS ${CLANG_TIDY_EXE}))
    message(STATUS "Using clang-tidy for linting")
    
    add_executable(market-data-collector-lint ${SRCFILES})
    set_target_properties(market-data-collector-lint PROPERTIES EXCLUDE_FROM_ALL 1)

    set_target_properties(market-data-collector-lint PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    if (MSVC)
        target_compile_options(market-data-collector-lint PRIVATE /W4)
    else()
        target_compile_options(market-data-collector-lint PRIVATE -Wall -Wextra -pedantic)
    endif()

    target_include_directories(market-data-collector-lint PRIVATE include)
    target_include_directories(market-data-collector-lint SYSTEM PRIVATE dependencies)
    
    target_include_directories(market-data-collector-lint SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(market-data-collector-lint PRIVATE ${Boost_LIBRARIES})

    target_include_directories(market-data-collector-lint SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(market-data-collector-lint PRIVATE ${OPENSSL_LIBRARIES})

    target_include_directories(market-data-collector-lint SYSTEM PRIVATE ${CURL_INCLUDE_DIR})
    target_link_libraries(market-data-collector-lint PRIVATE ${CURL_LIBRARIES})

    add_custom_target(lint DEPENDS market-data-collector-lint)

    set(CLANG_TIDY_COMMAND "${CLANG_TIDY_EXE}" "--checks=clang-analyzer-cplusplus*,concurrency-*,bugprone-*,boost-*,cppcoreguidelines-*")
    set_target_properties(market-data-collector-lint PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")
endif()