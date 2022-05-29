cmake_minimum_required(VERSION 3.0.0)
project(market-data-collector)

set(CMAKE_CXX_STANDARD 17)

add_executable(market-data-collector src/market_data_collector.cpp)
target_include_directories(market-data-collector PRIVATE include dependencies)

if (MSVC)
    # warning level 4
    add_compile_options(/W4)
else()
    # lots of warnings
    add_compile_options(-Wall -Wextra -pedantic)
endif()

find_package(Boost REQUIRED program_options log log_setup)
if(Boost_FOUND)
    target_include_directories(market-data-collector PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(market-data-collector PRIVATE ${Boost_LIBRARIES})
else()
    message(FATAL_ERROR "Required boost libraries are not found")
endif()

find_package(OpenSSL REQUIRED)
if(OpenSSL_FOUND)
    target_include_directories(market-data-collector PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(market-data-collector PRIVATE ${OPENSSL_LIBRARIES})
else()
    message(FATAL_ERROR "Openssl libraries are not found")
endif()

find_package(CURL REQUIRED)
if(CURL_FOUND)
    target_include_directories(market-data-collector PRIVATE ${CURL_INCLUDE_DIR})
    target_link_libraries(market-data-collector PRIVATE ${CURL_LIBRARIES})
else()
    message(FATAL_ERROR "Curl library is not found")
endif()
