set(TARGET wingman)
option(LLAMA_SERVER_VERBOSE "Build verbose logging option for Wingman" ON)
# set home directory depending on platform
if (WIN32)
    set(HOME_DIR $ENV{USERPROFILE})
else()
    set(HOME_DIR $ENV{HOME})
endif()
set(DESTINATION_DIR ${HOME_DIR}/source/repos/tt/tests/downloadllama/server/wingman/bin/win32)
# # IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
#     SET(CMAKE_INSTALL_PREFIX ${DESTINATION_DIR})
# # ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

find_path(UWEBSOCKETS_INCLUDE_DIRS "uwebsockets/App.h" REQUIRED)
find_library(USOCKETS_LIBRARY uSockets REQUIRED)
find_package(ZLIB REQUIRED)
find_package(libuv CONFIG REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${UWEBSOCKETS_INCLUDE_DIRS})
add_executable(${TARGET} wingman.cpp json.hpp httplib.h)
set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
# install(TARGETS ${TARGET} RUNTIME)
install(TARGETS ${TARGET} RUNTIME DESTINATION ${DESTINATION_DIR})
target_compile_definitions(${TARGET} PRIVATE
    WINGMAN_VERBOSE=$<BOOL:${LLAMA_SERVER_VERBOSE}>
)
target_link_libraries(${TARGET} PRIVATE 
    common llama
    ZLIB::ZLIB
    ${USOCKETS_LIBRARY}
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv> ${CMAKE_THREAD_LIBS_INIT})

if (WIN32)
    TARGET_LINK_LIBRARIES(${TARGET} PRIVATE ws2_32)
endif()
target_compile_features(${TARGET} PRIVATE cxx_std_11)
if(TARGET BUILD_INFO)
  add_dependencies(${TARGET} BUILD_INFO)
endif()
