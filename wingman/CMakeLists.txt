cmake_minimum_required(VERSION 3.21)

# project(wingman CXX)
set(TARGET wingman)
set(NEXT_TARGET dabarkus)

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
find_package(Annoy CONFIG REQUIRED)
find_path(RAPIDCSV_INCLUDE_DIRS "rapidcsv.h")
find_package(PkgConfig)
find_package(ICU REQUIRED COMPONENTS uc i18n io)

pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)

cmake_path(SET WINGMAN_LLAMA_CPP_SOURCE_DIR NORMALIZE ${CMAKE_CURRENT_SOURCE_DIR}/..)
cmake_path(SET EXAMPLE_SERVER_DIR NORMALIZE ${WINGMAN_LLAMA_CPP_SOURCE_DIR}/examples/server)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${WINGMAN_LLAMA_CPP_SOURCE_DIR} ${EXAMPLE_SERVER_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common ${UWEBSOCKETS_INCLUDE_DIRS} ${RAPIDCSV_INCLUDE_DIRS})

# Reference the public assets from EXAMPLE_SERVER_PUBLIC_DIR
set(PUBLIC_ASSETS
    index.html
    index.js
    completion.js
    json-schema-to-grammar.mjs
)

foreach(asset ${PUBLIC_ASSETS})
    set(input "${EXAMPLE_SERVER_DIR}/public/${asset}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${asset}.hpp")
    list(APPEND EMBEDDED_TARGET_SRCS ${output})
    add_custom_command(
        DEPENDS "${input}"
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${input}" "-DOUTPUT=${output}" -P "${WINGMAN_LLAMA_CPP_SOURCE_DIR}/scripts/xxd.cmake"
        COMMENT "Embedding ${asset} from example/server into the binary"
    )

    # message(STATUS "Embedding ${asset}: ${input} -> ${output}")
endforeach()

set(SHARED_HEADER_DIRS include)

message(STATUS "Shared header directories: ${SHARED_HEADER_DIRS}")

set(SHARED_target_link_libraries
    ZLIB::ZLIB
    ${USOCKETS_LIBRARY}
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    spdlog::spdlog_header_only
    fmt::fmt
    CURL::libcurl
    unofficial::sqlite3::sqlite3
    argparse::argparse
    Annoy::Annoy
    PkgConfig::POPPLER_CPP
    ICU::uc ICU::i18n ICU::io
    ${CMAKE_THREAD_LIBS_INIT}
    ggml_static common llama llava
)

# ## Dabarkus Library
set(DABARKUS_SOURCES
    libsrc/curl.cpp
    libsrc/orm.cpp
    libsrc/on_exit.cpp
    libsrc/download.service.cpp
    libsrc/types.cpp
    libsrc/modelcard.cpp
    libsrc/parse_evals.cpp
    libsrc/hwinfo.direct.cpp
    libsrc/inferable.cpp
    libsrc/mimetypes.cpp
    libsrc/metadata.cpp
    libsrc/ingest.cpp
    libsrc/wingman.service.cpp
    wingman.server.cpp
    wingman.server.integration.cpp
)

add_library(${NEXT_TARGET} ${EMBEDDED_TARGET_SRCS} ${DABARKUS_SOURCES})
target_include_directories(${NEXT_TARGET} PUBLIC ${SHARED_HEADER_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${NEXT_TARGET} PRIVATE ${SHARED_target_link_libraries})
target_compile_options(${NEXT_TARGET} PUBLIC -DANNOYLIB_MULTITHREADED_BUILD)

if(LLAMA_SERVER_SSL)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${NEXT_TARGET} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(${NEXT_TARGET} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
endif()

# ## Wingman Executable
if(WINGMAN_BUILD_STANDALONE)
    set(WINGMAN_SOURCES ${EMBEDDED_TARGET_SRCS} wingman.server.cpp)
else()
    add_compile_definitions(WINGMAN_LIB)
    message(STATUS "Building Wingman inference as a library")
    set(WINGMAN_SOURCES wingman.cpp)
endif()

add_executable(${TARGET} ${WINGMAN_SOURCES})
target_include_directories(${TARGET} PRIVATE ${SHARED_HEADER_DIRS})
target_compile_definitions(${TARGET} PRIVATE
    WINGMAN_VERBOSE=$<BOOL:${LLAMA_SERVER_VERBOSE}>
)

target_link_libraries(${TARGET} PRIVATE
    ${NEXT_TARGET}
    ${SHARED_target_link_libraries}
    ${CMAKE_THREAD_LIBS_INIT})

# Define the path to the .dist directory
set(DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist")

# Add a custom command to copy dist folder content if it exists at build time
add_custom_command(
    DEPENDS ${DIST_DIR}
    TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Checking if dist directory exists..."
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${DIST_DIR}" "$<TARGET_FILE_DIR:${TARGET}>/dist"
    COMMENT "Copying dist folder to binary output directory next to the executable"
    COMMAND_EXPAND_LISTS
)

# Install the .dist directory contents to the same directory as the executable
install(DIRECTORY ${DIST_DIR}/
    DESTINATION $<TARGET_FILE_DIR:${TARGET}>
    COMPONENT Runtime)

# add_executable(${TARGET}_launcher wingman_launcher.cpp)
# target_include_directories(${TARGET}_launcher PRIVATE ${SHARED_HEADER_DIRS})
# target_compile_definitions(${TARGET}_launcher PRIVATE
# WINGMAN_VERBOSE=$<BOOL:${LLAMA_SERVER_VERBOSE}>
# )

# target_link_libraries(${TARGET}_launcher PRIVATE
# common llama llava
# $<TARGET_OBJECTS:${TARGET}_lib>
# tiny-process-library::tiny-process-library
# # Boost::filesystem
# ${SHARED_target_link_libraries}
# ${CMAKE_THREAD_LIBS_INIT})
add_executable(${TARGET}_reset wingman_reset.cpp)
target_include_directories(${TARGET}_reset PRIVATE ${SHARED_HEADER_DIRS})
target_compile_definitions(${TARGET}_reset PRIVATE
    WINGMAN_VERBOSE=$<BOOL:${LLAMA_SERVER_VERBOSE}>
)

target_link_libraries(${TARGET}_reset PRIVATE
    ${NEXT_TARGET}
    ${SHARED_target_link_libraries}
    ${CMAKE_THREAD_LIBS_INIT})

if(MSVC)
    target_compile_options(${TARGET} PRIVATE /bigobj)
    target_compile_options(${NEXT_TARGET} PRIVATE /bigobj)
endif()

if(WIN32)
    target_link_libraries(${TARGET} PRIVATE ws2_32)
    target_link_libraries(${NEXT_TARGET} PRIVATE ws2_32)
endif()

# Define common compile options
set(NIX_COMPILE_OPTIONS -Wno-unused-parameter -Wno-missing-field-initializers -Wno-missing-prototypes -Wno-unknown-pragmas -Wno-missing-declarations)
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")

if(MSVC)
# Specify MSVC-specific compile options if necessary
# Example: set(MSVC_COMPILE_OPTIONS /W4)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "AppleClang") # Add common compile options for both GCC and Clang
    message(STATUS "Adding common compile options for GCC and Clang compilers")
    target_compile_options(${NEXT_TARGET} PRIVATE ${NIX_COMPILE_OPTIONS})
    target_compile_options(${TARGET} PRIVATE ${NIX_COMPILE_OPTIONS})
    target_compile_options(${TARGET}_reset PRIVATE ${NIX_COMPILE_OPTIONS})

    # Check for Clang to add specific flags
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(STATUS "Adding Clang-specific compile options")
        target_compile_options(${NEXT_TARGET} PRIVATE -Wc99-extensions)
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
