cmake_minimum_required(VERSION 3.21)

project(wingman C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# set home directory depending on platform
if (WIN32)
    set(HOME_DIR $ENV{USERPROFILE})
else()
    set(HOME_DIR $ENV{HOME})
endif()

if(WINGMAN_BUILD_TESTS)
    include(CTest)
    # enable testing
    enable_testing()
endif()

find_library(USOCKETS_LIBRARY uSockets REQUIRED)
find_path(UWEBSOCKETS_INCLUDE_DIRS "uwebsockets/App.h" REQUIRED)
find_package(ZLIB REQUIRED)
if (APPLE)
    find_package(unofficial-libuv CONFIG REQUIRED)
else()
    find_package(libuv CONFIG REQUIRED)
endif()
find_package(spdlog CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)
find_package(OpenCL REQUIRED)
find_package(OpenGL REQUIRED)

if (WINGMAN_BUILD_STANDALONE)
else()
    add_compile_definitions(WINGMAN_LIB)
    message(STATUS "Building Wingman as a library")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${UWEBSOCKETS_INCLUDE_DIRS})

set(TARGET wingman)

set(SHARED_HEADER_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/llama_cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/llava
)

#include_directories(${SHARED_HEADER_DIRS})
message(STATUS "Shared header directories: ${SHARED_HEADER_DIRS}")

set(${TARGET}_lib_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/curl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/orm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/on_exit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/download.service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/opencl.info.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/opengl.info.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/types.cpp
)
if (APPLE)
set ($(TARGET)_LIBUV unofficial::libuv::libuv)
else()
set ($(TARGET)_LIBUV $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)
endif()

set(SHARED_target_link_libraries
    ZLIB::ZLIB
    ${USOCKETS_LIBRARY}
    $(TARGET)_LIBUV
    spdlog::spdlog_header_only
    fmt::fmt
    CURL::libcurl
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
    argparse::argparse
    OpenCL::OpenCL
    OpenGL::GL
    ${CMAKE_THREAD_LIBS_INIT}
)

### Wingman Library
add_library(${TARGET}_lib OBJECT ${${TARGET}_lib_SOURCES})
target_include_directories(${TARGET}_lib PUBLIC ${SHARED_HEADER_DIRS})
set_property(TARGET ${TARGET}_lib PROPERTY CXX_STANDARD 20)

if (WINGMAN_BUILD_STANDALONE)
    set(WINGMAN_SOURCES wingman.server.cpp)
else()
    add_compile_definitions(WINGMAN_LIB)
    message(STATUS "Building Wingman inference as a library")
    set(WINGMAN_SOURCES wingman.cpp wingman.server.cpp wingman.server.integration.cpp libsrc/wingman.service.cpp)
endif()

add_executable(${TARGET} ${WINGMAN_SOURCES})
target_include_directories(${TARGET} PRIVATE ${SHARED_HEADER_DIRS})
set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
install(TARGETS ${TARGET} RUNTIME)
target_compile_definitions(${TARGET} PRIVATE
    WINGMAN_VERBOSE=$<BOOL:${LLAMA_SERVER_VERBOSE}>
)
target_link_libraries(${TARGET} PRIVATE
    common llava
	$<TARGET_OBJECTS:${TARGET}_lib>
    ${SHARED_target_link_libraries}
    ${CMAKE_THREAD_LIBS_INIT})

if (WIN32)
    target_link_libraries(${TARGET} PRIVATE ws2_32)
endif()
target_compile_features(${TARGET} PRIVATE cxx_std_11)
if (APPLE)
    target_compile_options(${TARGET} PRIVATE -Wno-unused-parameter -Wno-dev -std=c++20)
endif()
if(TARGET BUILD_INFO)
    add_dependencies(${TARGET} BUILD_INFO)
endif()

### Tests
if (WINGMAN_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ## Tools
if (WINGMAN_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
