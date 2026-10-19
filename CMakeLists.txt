cmake_minimum_required(VERSION 3.28.3)
project(KernelLoader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
else()
    add_compile_options(-g -O0)
endif()

option(ANDROID_MODE "Legacy Android mode" OFF)
if (${ANDROID_MODE})
    set(ANDROID ON)
    add_compile_definitions(__ANDROID__)
    # The NDK toolchain supplies its own sysroot.
endif ()

add_subdirectory(lib/spdlog-1.17.0)

add_library(KernelLoader SHARED
        src/core.cpp
        src/logger.cpp
        src/gui.cpp
        src/gui_images.cpp
        src/gui_ime.cpp
        includes/tefkernel/tef_api_imp.c
)

target_include_directories(KernelLoader
        PUBLIC
        includes
        mod-api
)

target_link_libraries(KernelLoader
        PRIVATE spdlog::spdlog
)


if (CMAKE_BUILD_TYPE STREQUAL "Release")
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
        add_custom_command(TARGET KernelLoader POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "Stripping symbols from KernelLoader"
                COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:KernelLoader>
                COMMENT "Stripping symbols from KernelLoader library using strip command"
                VERBATIM
        )
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # macOS 使用不同的 strip 选项
        add_custom_command(TARGET KernelLoader POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "Stripping symbols from KernelLoader..."
                COMMAND ${CMAKE_STRIP} -S $<TARGET_FILE:KernelLoader>
                COMMENT "Stripping symbols from KernelLoader library using strip command"
                VERBATIM
        )
    endif()
endif()


if(ANDROID)
    set(SYSTEM_NAME "android")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SYSTEM_NAME "linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(SYSTEM_NAME "darwin")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SYSTEM_NAME "windows")
else()
    set(SYSTEM_NAME "unknown")
endif()

# 获取架构

if(MINGW OR WIN32)
    # Windows: 通过编译器名称判断架构
    if(CMAKE_C_COMPILER MATCHES "x86_64-w64-mingw32" OR CMAKE_CXX_COMPILER MATCHES "x86_64-w64-mingw32")
        set(ARCH "x64")
    elseif(CMAKE_C_COMPILER MATCHES "i686-w64-mingw32" OR CMAKE_CXX_COMPILER MATCHES "i686-w64-mingw32")
        set(ARCH "x86")
    else()
        # 备用方案：通过指针大小判断
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(ARCH "x64")
        else()
            set(ARCH "x86")
        endif()
    endif()
else()
    # 非 Android 平台使用简化的架构名称
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(ARCH "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|armv7a|ARMv7|arm")
        set(ARCH "arm")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64|x64")
        set(ARCH "x64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i686|i386|x86")
        set(ARCH "x86")
    else()
        set(ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
endif()

#[[
message(STATUS "========== DEBUG ==========")
message(STATUS "SYSTEM_NAME: ${SYSTEM_NAME}")
message(STATUS "ARCH: ${ARCH}")
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "WIN32: ${WIN32}")
message(STATUS "MINGW: ${MINGW}")
message(STATUS "============================")]]


# 设置输出文件名

set_target_properties(KernelLoader PROPERTIES
    OUTPUT_NAME "loader.${SYSTEM_NAME}.${ARCH}"
)

if(ANDROID)
    add_subdirectory(lib/gui-xdl)
    set_target_properties(xdl PROPERTIES C_VISIBILITY_PRESET hidden)
    target_link_libraries(KernelLoader PRIVATE xdl ${CMAKE_DL_LIBS})
endif()
option(KERNELLOADER_BUILD_GUI_DEMO "Build native GUI test mod" OFF)
if(KERNELLOADER_BUILD_GUI_DEMO)
    add_library(gui_demo SHARED examples/gui_demo/gui_demo.cpp)
    target_include_directories(gui_demo PRIVATE mod-api)
    target_compile_definitions(gui_demo PRIVATE BUILDING_DLL=1)
    if(WIN32)
        set_target_properties(gui_demo PROPERTIES PREFIX "" OUTPUT_NAME "gui_demo.windows.${ARCH}")
    elseif(APPLE)
        set_target_properties(gui_demo PROPERTIES OUTPUT_NAME "gui_demo.macos.${ARCH}")
    else()
        set_target_properties(gui_demo PROPERTIES OUTPUT_NAME "gui_demo.${SYSTEM_NAME}.${ARCH}")
    endif()
endif()
option(KERNELLOADER_BUILD_GUI_TESTS "Build host GUI runtime tests" OFF)
if(KERNELLOADER_BUILD_GUI_TESTS AND NOT ANDROID AND NOT WIN32)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(gui_runtime_test tests/gui_runtime_test.cpp src/gui_images.cpp src/gui_ime.cpp includes/tefkernel/tef_api_imp.c)
    target_include_directories(gui_runtime_test PRIVATE includes mod-api)
    target_link_libraries(gui_runtime_test PRIVATE spdlog::spdlog Threads::Threads)
    target_compile_options(gui_runtime_test PRIVATE -UNDEBUG)
    add_test(NAME gui_runtime_test COMMAND gui_runtime_test)
endif()

if(MINGW)
    target_link_options(KernelLoader PRIVATE -static)
    target_link_options(KernelLoader PRIVATE -Wl,--export-all-symbols)
    if(TARGET gui_demo)
        target_link_options(gui_demo PRIVATE -static)
    endif()
endif()
if(KERNELLOADER_BUILD_GUI_TESTS AND NOT ANDROID AND NOT WIN32)
    add_executable(gui_android_input_test tests/gui_android_input_test.cpp src/gui_ime.cpp includes/tefkernel/tef_api_imp.c)
    target_include_directories(gui_android_input_test PRIVATE includes mod-api)
    target_link_libraries(gui_android_input_test PRIVATE spdlog::spdlog Threads::Threads)
    target_compile_definitions(gui_android_input_test PRIVATE __ANDROID__=1)
    target_compile_options(gui_android_input_test PRIVATE -UNDEBUG)
    add_test(NAME gui_android_input_test COMMAND gui_android_input_test)
endif()


file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/ime/pinyin.tsv" GUI_PINYIN_DATA)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gui_pinyin_data.hpp" "#pragma once\nstatic const char gui_pinyin_data[] = R\"KL_IME(${GUI_PINYIN_DATA})KL_IME\";\n")
target_include_directories(KernelLoader PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(KERNELLOADER_BUILD_GUI_TESTS AND NOT ANDROID AND NOT WIN32)
    foreach(t gui_runtime_test gui_android_input_test)
        target_include_directories(${t} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    endforeach()
    add_executable(gui_ime_test tests/gui_ime_test.cpp src/gui_ime.cpp)
    target_include_directories(gui_ime_test PRIVATE includes "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_options(gui_ime_test PRIVATE -UNDEBUG)
    add_test(NAME gui_ime_test COMMAND gui_ime_test)
endif()
