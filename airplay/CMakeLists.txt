cmake_minimum_required(VERSION 3.16)
project(fidelio-raop CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static mbedTLS built by the cspot step (install prefix with lib/ and include/)
set(MBEDTLS_PREFIX "/home/user/sysroot-cspot" CACHE PATH "mbedTLS install prefix")

file(GLOB ALAC_SRC alac/*.c alac/*.cpp)
add_library(alacdec STATIC ${ALAC_SRC})
target_include_directories(alacdec PUBLIC alac)
target_compile_options(alacdec PRIVATE -O2 -Wno-unused -Wno-deprecated-declarations)

file(GLOB SRC src/*.cpp)
if(NOT APPLE)
  list(FILTER SRC EXCLUDE REGEX "AudioQueueSink")
endif()
add_executable(fidelio-raop ${SRC})
target_include_directories(fidelio-raop PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../common ${MBEDTLS_PREFIX}/include)
target_compile_options(fidelio-raop PRIVATE -O2 -Wall -Wno-unused-function)
target_link_libraries(fidelio-raop PRIVATE alacdec
  ${MBEDTLS_PREFIX}/lib/libmbedtls.a ${MBEDTLS_PREFIX}/lib/libmbedx509.a ${MBEDTLS_PREFIX}/lib/libmbedcrypto.a
)
if(APPLE)
  target_link_libraries(fidelio-raop PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
else()
  target_link_libraries(fidelio-raop PRIVATE pthread)
endif()
