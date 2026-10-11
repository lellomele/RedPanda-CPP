add_executable(redpanda-win-git-askpass WIN32 main.cpp redpanda-git-askpass_private.rc)

target_compile_definitions(redpanda-win-git-askpass PRIVATE
    ${GLOBAL_COMPILE_DEFINITIONS}
    UNICODE _UNICODE)

target_include_directories(redpanda-win-git-askpass PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(redpanda-win-git-askpass PRIVATE user32 shell32)

if(MINGW)
    target_link_options(redpanda-win-git-askpass PRIVATE -municode)
endif()

if(MSVC)
    target_link_libraries(redpanda-win-git-askpass PRIVATE
        advapi32.lib
        comdlg32.lib
        gdi32.lib
        shell32.lib
        user32.lib
        ws2_32.lib)
endif()

install(TARGETS redpanda-win-git-askpass
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .)

add_test(NAME test-windows-askpass-encoding COMMAND redpanda-win-git-askpass --self-test)
set_tests_properties(test-windows-askpass-encoding PROPERTIES TIMEOUT 15 LABELS "infrastructure;functional")
add_dependencies(all-test-targets redpanda-win-git-askpass)
