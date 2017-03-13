cmake_minimum_required(VERSION 3.0)
project(libhttp)

include(deps.cmake)

add_library(libhttp
    src/hpack.hpp src/hpack_unhuff.hpp src/hpack.cpp
    include/http_server.hpp src/http_server.cpp src/http2_server.cpp
    src/utils.hpp src/utils.cpp
    )

target_include_directories(libhttp PUBLIC include)
target_link_libraries(libhttp string_view avakar_libstream)
