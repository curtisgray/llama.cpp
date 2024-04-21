cmake_minimum_required(VERSION 3.21)

project(wingman CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(Boost_NO_BOOST_CMAKE OFF) # TODO: remove this line when CMake is updated to the latest Boost version

# set home directory depending on platform
if(WIN32)
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
find_package(libuv CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)
#find_package(tiny-process-library CONFIG REQUIRED)
find_path(RAPIDCSV_INCLUDE_DIRS "rapidcsv.h")

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../examples/server ${CMAKE_CURRENT_SOURCE_DIR}/../common ${UWEBSOCKETS_INCLUDE_DIRS} ${RAPIDCSV_INCLUDE_DIRS})

set(TARGET wingman)

set(SHARED_HEADER_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/llama_cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/llava
)

message(STATUS "Shared header directories: ${SHARED_HEADER_DIRS}")

set(${TARGET}_lib_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/curl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/orm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/on_exit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/download.service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/modelcard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/parse_evals.cpp
)

set(SHARED_target_link_libraries
    ZLIB::ZLIB
    ${USOCKETS_LIBRARY}
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    spdlog::spdlog_header_only
    fmt::fmt
    CURL::libcurl
    unofficial::sqlite3::sqlite3
    argparse::argparse
    ${CMAKE_THREAD_LIBS_INIT}
)

# ## Wingman Library
add_library(${TARGET}_lib OBJECT ${${TARGET}_lib_SOURCES})
target_include_directories(${TARGET}_lib PUBLIC ${SHARED_HEADER_DIRS})

if(WINGMAN_BUILD_STANDALONE)
    set(WINGMAN_SOURCES wingman.server.cpp)
else()
    add_compile_definitions(WINGMAN_LIB)
    message(STATUS "Building Wingman inference as a library")
    set(WINGMAN_SOURCES wingman.cpp wingman.server.cpp wingman.server.integration.cpp libsrc/wingman.service.cpp)
endif()

add_executable(${TARGET} ${WINGMAN_SOURCES})
target_include_directories(${TARGET} PRIVATE ${SHARED_HEADER_DIRS})
target_compile_definitions(${TARGET} PRIVATE
    WINGMAN_VERBOSE=$<BOOL:${LLAMA_SERVER_VERBOSE}>
)
if (MSVC)
    target_compile_options(${TARGET} PRIVATE /bigobj)
endif ()
target_link_libraries(${TARGET} PRIVATE
    common llama llava
    $<TARGET_OBJECTS:${TARGET}_lib>
    ${SHARED_target_link_libraries}
    ${CMAKE_THREAD_LIBS_INIT})

if (LLAMA_SERVER_SSL)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${TARGET} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(${TARGET} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
endif()

if(WIN32)
    target_link_libraries(${TARGET} PRIVATE ws2_32)
endif()

#add_executable(${TARGET}_launcher wingman_launcher.cpp)
#target_include_directories(${TARGET}_launcher PRIVATE ${SHARED_HEADER_DIRS})
#target_compile_definitions(${TARGET}_launcher PRIVATE
#    WINGMAN_VERBOSE=$<BOOL:${LLAMA_SERVER_VERBOSE}>
#)

#target_link_libraries(${TARGET}_launcher PRIVATE
#    common llama llava
#    $<TARGET_OBJECTS:${TARGET}_lib>
#    tiny-process-library::tiny-process-library
#    # Boost::filesystem
#    ${SHARED_target_link_libraries}
#    ${CMAKE_THREAD_LIBS_INIT})

add_executable(${TARGET}_reset wingman_reset.cpp)
target_include_directories(${TARGET}_reset PRIVATE ${SHARED_HEADER_DIRS})
target_compile_definitions(${TARGET}_reset PRIVATE
   WINGMAN_VERBOSE=$<BOOL:${LLAMA_SERVER_VERBOSE}>
)

target_link_libraries(${TARGET}_reset PRIVATE
   $<TARGET_OBJECTS:${TARGET}_lib>
   ${SHARED_target_link_libraries}
   ${CMAKE_THREAD_LIBS_INIT})

# Define common compile options
set(NIX_COMPILE_OPTIONS -Wno-unused-parameter -Wno-missing-field-initializers -Wno-missing-prototypes -Wno-unknown-pragmas -Wno-missing-declarations)

if(MSVC)
# Specify MSVC-specific compile options if necessary
# Example: set(MSVC_COMPILE_OPTIONS /W4)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Add common compile options for both GCC and Clang
    message(STATUS "Adding common compile options for GCC and Clang compilers")
    target_compile_options(${TARGET}_lib PRIVATE ${NIX_COMPILE_OPTIONS})
    target_compile_options(${TARGET} PRIVATE ${NIX_COMPILE_OPTIONS})
    target_compile_options(${TARGET}_reset PRIVATE ${NIX_COMPILE_OPTIONS})

    # Check for Clang to add specific flags
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(STATUS "Adding Clang-specific compile options")
        target_compile_options(${TARGET}_lib PRIVATE -Wc99-extensions)
        target_compile_options(${TARGET} PRIVATE -Wc99-extensions)
        target_compile_options(${TARGET}_reset PRIVATE -Wc99-extensions)
    endif()
else()
    message(STATUS "Compiler not supported explicitly in CMake configuration.")
endif()

if(TARGET BUILD_INFO)
    add_dependencies(${TARGET} BUILD_INFO)
endif()

if(WIN32 AND CUDAToolkit_FOUND)
    # List of patterns for the CUDA DLLs you're interested in
    # This allows for easy expansion if more DLL types need to be handled in the future
    set(CUDA_DLL_PATTERNS
        "cudart*.dll"
        "cublas*.dll"
        "cublasLt*.dll"
    )

    # Iterate over each pattern and install the matching files
    foreach(PATTERN IN LISTS CUDA_DLL_PATTERNS)
        # Glob for files matching the current pattern
        file(GLOB MATCHED_DLLS
            LIST_DIRECTORIES false
            "${CUDAToolkit_BIN_DIR}/${PATTERN}")

        # Install the matched DLLs if any are found
        if(MATCHED_DLLS)
            install(FILES ${MATCHED_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif()
    endforeach()
endif()

# install(TARGETS ${TARGET} ${TARGET}_launcher RUNTIME)
# install(TARGETS ${TARGET} RUNTIME)
install(TARGETS ${TARGET} ${TARGET}_reset RUNTIME)

# ## Tests
if(WINGMAN_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ## Tools
if(WINGMAN_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
