function(wingman_build_tool source)
    get_filename_component(TOOL_TARGET ${source} NAME_WLE)
    add_executable(${TOOL_TARGET} ${source})
    target_include_directories(${TOOL_TARGET} PRIVATE ${SHARED_HEADER_DIRS})
    set_property(TARGET ${TOOL_TARGET} PROPERTY CXX_STANDARD 20)
    install(TARGETS ${TOOL_TARGET} RUNTIME)
    target_link_libraries(${TOOL_TARGET} PRIVATE ${TOOL_LINK_LIBRARIES})
    if(WIN32)
        target_link_libraries(${TOOL_TARGET} PRIVATE ws2_32)
    endif()
endfunction()

set(TOOL_LINK_LIBRARIES $<TARGET_OBJECTS:${TARGET}_lib> ${SHARED_target_link_libraries})

# run `wingman_build_tool` for each tool in the tools directory
file(GLOB tools RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} tool.*.cpp)
foreach(tool ${tools})
	wingman_build_tool(${tool})
endforeach()
