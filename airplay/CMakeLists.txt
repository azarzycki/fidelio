cmake_minimum_required(VERSION 3.16)
project(dock-raop CXX C)
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
# Shared with dock-audiod and cspot-ios3: the audiod wire framing and the SocketSink output.
set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../common)
add_executable(dock-raop ${SRC} ${COMMON}/AudiodFraming.cpp ${COMMON}/SocketSink.cpp ${COMMON}/Settings.cpp)
target_include_directories(dock-raop PRIVATE src ${COMMON} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${MBEDTLS_PREFIX}/include)
target_compile_options(dock-raop PRIVATE -O2 -Wall -Wno-unused-function)
target_link_libraries(dock-raop PRIVATE alacdec
  ${MBEDTLS_PREFIX}/lib/libmbedtls.a ${MBEDTLS_PREFIX}/lib/libmbedx509.a ${MBEDTLS_PREFIX}/lib/libmbedcrypto.a
)
if(APPLE)
  target_link_libraries(dock-raop PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
else()
  target_link_libraries(dock-raop PRIVATE pthread)
endif()
