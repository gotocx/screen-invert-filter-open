cmake_minimum_required(VERSION 3.20)

project(ScreenInvertFilter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ScreenInvertFilter WIN32
  src/main.cpp
)

target_compile_definitions(ScreenInvertFilter PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if (MSVC)
  target_compile_options(ScreenInvertFilter PRIVATE /utf-8)
endif()

target_link_libraries(ScreenInvertFilter PRIVATE
  user32
  gdi32
  Magnification
)
