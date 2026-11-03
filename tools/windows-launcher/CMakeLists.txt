# Standalone project, not part of the QK4 build. The launcher sits at the install root and is never replaced by an
# update, so it has no Qt dependency and a statically linked C runtime.
cmake_minimum_required(VERSION 3.21)
project(QK4Launcher LANGUAGES CXX RC)

option(QK4_LAUNCHER_BUILD_TESTS "Build the integration test fixtures and the test-pause launcher" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(LAUNCHER_SOURCES main.cpp updatelayout.cpp launcher.rc)

function(qk4_launcher_target name)
    add_executable(${name} WIN32 ${LAUNCHER_SOURCES})
    target_compile_definitions(${name} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(${name} PRIVATE /W4 /utf-8)
    target_link_libraries(${name} PRIVATE bcrypt)
endfunction()

qk4_launcher_target(qk4_launcher)
set_target_properties(qk4_launcher PROPERTIES OUTPUT_NAME QK4)

if(QK4_LAUNCHER_BUILD_TESTS)
    # Never built for a release: the pause only exists in this separate binary.
    qk4_launcher_target(qk4_launcher_testpause)
    set_target_properties(qk4_launcher_testpause PROPERTIES OUTPUT_NAME QK4-testpause)
    target_compile_definitions(qk4_launcher_testpause PRIVATE QK4_LAUNCHER_TEST_PAUSE)

    add_executable(fakeapp WIN32 tests/fakeapp.cpp)
    target_compile_definitions(fakeapp PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(fakeapp PRIVATE /utf-8)
    target_link_libraries(fakeapp PRIVATE user32 shell32)

    find_package(Qt6 REQUIRED COMPONENTS Core)
    add_executable(qtrelaunchapp WIN32 tests/qtrelaunchapp.cpp)
    set_target_properties(qtrelaunchapp PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_link_libraries(qtrelaunchapp PRIVATE Qt6::Core Qt6::EntryPointPrivate)
endif()
