cmake_minimum_required(VERSION 3.20)
project(KeyboardLayoutFix LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the DLL has no runtime dependencies beyond Windows itself.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Optional: path to a game's ue4ss\Mods folder; the built mod is copied there.
set(KLF_INSTALL_TO "" CACHE PATH "UE4SS Mods folder to copy the mod into after building")

include(FetchContent)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG 8af6b4acae5a9388fd742b56fa79ece89d96f823
    # Nonexistent subdir: download only, don't add MinHook's own CMakeLists.
    SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(minhook)

# Build MinHook from its sources directly instead of relying on its own CMake setup.
add_library(minhook STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

add_library(KeyboardLayoutFix SHARED src/dllmain.cpp)
target_link_libraries(KeyboardLayoutFix PRIVATE minhook)
target_compile_definitions(KeyboardLayoutFix PRIVATE UNICODE _UNICODE NOMINMAX)
# UE4SS loads <mod>/dlls/main.dll.
set_target_properties(KeyboardLayoutFix PROPERTIES OUTPUT_NAME main)

# Assemble the ready-to-drop mod folder: dist/KeyboardLayoutFix/{dlls/main.dll, config.ini, enabled.txt}
set(KLF_DIST ${CMAKE_SOURCE_DIR}/dist/KeyboardLayoutFix)
add_custom_command(TARGET KeyboardLayoutFix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${KLF_DIST}/dlls
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:KeyboardLayoutFix> ${KLF_DIST}/dlls/main.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/mod/config.ini ${KLF_DIST}/config.ini
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/mod/enabled.txt ${KLF_DIST}/enabled.txt
    # MinHook is BSD-2: its notice must ship with the binary.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${minhook_SOURCE_DIR}/LICENSE.txt ${KLF_DIST}/MinHook-LICENSE.txt)

# Smoke test: `ctest -C Release` from the build dir (needs the Czech layout available).
enable_testing()
add_executable(smoke tests/smoke.cpp)
add_dependencies(smoke KeyboardLayoutFix)
add_test(NAME smoke COMMAND smoke ${KLF_DIST}/dlls/main.dll)

if(KLF_INSTALL_TO)
    add_custom_command(TARGET KeyboardLayoutFix POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${KLF_INSTALL_TO}/KeyboardLayoutFix/dlls"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:KeyboardLayoutFix> "${KLF_INSTALL_TO}/KeyboardLayoutFix/dlls/main.dll"
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/mod/enabled.txt "${KLF_INSTALL_TO}/KeyboardLayoutFix/enabled.txt")
endif()
