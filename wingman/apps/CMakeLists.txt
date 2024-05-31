function(app_build source)
    get_filename_component(APP_TARGET ${source} NAME_WLE)
    add_executable(${APP_TARGET} ${source})
    target_include_directories(${APP_TARGET} PRIVATE ${SHARED_HEADER_DIRS})
    set_property(TARGET ${APP_TARGET} PROPERTY CXX_STANDARD 20)
    install(TARGETS ${APP_TARGET} RUNTIME)
    target_link_libraries(${APP_TARGET} PRIVATE ${APP_LINK_LIBRARIES})

    if(WIN32)
        target_link_libraries(${APP_TARGET} PRIVATE ws2_32)
    endif()

    if(MSVC)
        target_compile_options(${APP_TARGET} PRIVATE /bigobj)
    endif()
endfunction()

set(APP_LINK_LIBRARIES ${DABARQUS})

# run `app_build` for each tool in the tools directory
file(GLOB tools RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cpp)
foreach(tool ${tools})
	app_build(${tool})
endforeach()
