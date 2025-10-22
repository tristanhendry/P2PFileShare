cmake_minimum_required(VERSION 3.16)
project(p2p_bittorrent CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic)

include_directories(include)

file(GLOB_RECURSE P2P_SRC CONFIGURE_DEPENDS src/*.cpp)
add_executable(peerProcess ${P2P_SRC})

if(APPLE)
  # nothing special
elseif(UNIX)
  target_link_libraries(peerProcess pthread)
elseif(WIN32)
  target_link_libraries(peerProcess ws2_32)
endif()
