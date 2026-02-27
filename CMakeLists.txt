cmake_minimum_required(VERSION 3.21)
# Set the Project Name.
set(PROJECT_NAME                EnigmaFix)
# Set the name of the target operating system.
set(CMAKE_SYSTEM_NAME           Windows)
set(MINGW_TOOLCHAIN_PREFIX      x86_64-w64-mingw32)

set(ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/EnigmaFix_Icon.ico")
set(RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Resources.rc")

# If using MinGW, add the necessary compile definition so WinUser.h works properly.
if(MINGW)
    add_compile_definitions(_WIN32_WINNT=0x0605 WINVER=0x0605)
endif()

if (UNIX) # If compiling on Linux, manually configure MingW-64.
    set(CMAKE_C_COMPILER ${MINGW_TOOLCHAIN_PREFIX}-gcc)
    set(CMAKE_CXX_COMPILER ${MINGW_TOOLCHAIN_PREFIX}-g++)
    set(CMAKE_RC_COMPILER ${MINGW_TOOLCHAIN_PREFIX}-windres)
    set(CMAKE_LINKER ${MINGW_TOOLCHAIN_PREFIX}-ld)
    set(CMAKE_AR ${MINGW_TOOLCHAIN_PREFIX}-ar)
    # Adjust the default behavior of the FIND_XXX() commands:
    # Never search for programs in the host environment
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    # Search for headers and libraries only in the target environment
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    # Add Boost Include Directory (For MinGW).
    #set(Boost_INCLUDE_DIR ${CMAKE_FIND_ROOT_PATH}/sys-root/mingw/include/)
endif (UNIX)

# Set our output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Binaries/${CMAKE_SYSTEM_NAME})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Binaries/${CMAKE_SYSTEM_NAME})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Binaries/${CMAKE_SYSTEM_NAME})

# Set our intermediate directories.
set(CMAKE_BINARY_DIR               ${CMAKE_CURRENT_SOURCE_DIR}/Intermediate/${CMAKE_BUILD_TYPE})
set(CMAKE_BUILD_FILES_DIRECTORY    ${CMAKE_CURRENT_SOURCE_DIR}/Intermediate/${CMAKE_BUILD_TYPE})

# Set our compile method to Ninja.
set(CMAKE_GENERATOR Ninja)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD   23)

# Enable NASM support.
enable_language(ASM_NASM)

# Set a definition for a debug build through CMake. Also includes GDB debugging information.
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -DDEBUG -ggdb3")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -DDEBUG -ggdb3")

# Set compile flags for release builds, this should strip the debug symbols and optimizes it.
if(MINGW)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -s")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -s")
elseif(MSVC)
    # MSVC uses its own optimization flags
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
endif()

# Uncomment these flags if you need verbose output for whatever reason.
#set(CMAKE_VERBOSE_MAKEFILE ON)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -v")

## Setup Boost (Still need to sort out the error regarding "the target platform not supporting dynamic linking"
#find_package(Boost REQUIRED COMPONENTS) # Add Boost Components Here.
# Include Boost directories
#include_directories(${Boost_INCLUDE_DIRS})

## Zydis
set(ZYDIS_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/Source/ThirdParty/Zydis")
message(STATUS "Using Zydis from submodule...")
add_subdirectory("${ZYDIS_SOURCE_DIR}")

## SafetyHook
option(SAFETYHOOK_FETCH_ZYDIS "Option to fetch Zydis" OFF)
option(SAFETYHOOK_BUILD_DOCS "Option to generate documentation" OFF)
### Pass the option to the compiler
if(SAFETYHOOK_FETCH_ZYDIS)
    add_definitions(-DSAFETYHOOK_FETCH_ZYDIS)
endif()
set(SAFETYHOOK_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/Source/ThirdParty/SafetyHook")
message(STATUS "Using SafetyHook from submodule...")
add_subdirectory("${SAFETYHOOK_SOURCE_DIR}")

## SpdLog
set(SPDLOG_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/Source/ThirdParty/SpdLog")
message(STATUS "Using SpdLog from submodule...")
add_subdirectory("${SPDLOG_SOURCE_DIR}")
target_include_directories(spdlog PRIVATE "${SPDLOG_SOURCE_DIR}/include")

## ImGui
set(IMGUI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/Source/ThirdParty/ImGui")
message(STATUS "Using ImGui from submodule...")
add_library(imgui STATIC
        ${IMGUI_SOURCE_DIR}/imgui.cpp
        ${IMGUI_SOURCE_DIR}/imgui.h
        ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
        ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
        ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
        ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
)
add_library(imguibackends STATIC
        ${IMGUI_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${IMGUI_SOURCE_DIR}/backends/imgui_impl_dx11.h
        ${IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.h
)
### Include the ImGui header files with the ImGuiBackends, as we don't want to manually modify the includes to add a "../".
target_include_directories(imguibackends PUBLIC ${IMGUI_SOURCE_DIR})
target_link_libraries(imguibackends d3dcompiler dwmapi)
### We then finally link together ImGui and ImGuiBackends, so they're dependent on each other.
target_link_libraries(imgui imguibackends)

## MinHook (Required for Kiero)
FetchContent_Declare(
        minhook
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/Source/ThirdParty/MinHook
)
FetchContent_MakeAvailable(minhook)
message(STATUS "Using MinHook from submodule...")

## Kiero
set(kiero_backend "D3D11" CACHE STRING "Set Kiero backend")
set(kiero_minhook ON CACHE BOOL "Enable MinHook support" FORCE)
set(KIERO_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/Source/ThirdParty/Kiero-CMake")
message(STATUS "Using SafetyHook from submodule...")
add_subdirectory("${KIERO_SOURCE_DIR}")

## IniPP
set(INIPP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/Source/ThirdParty/IniPP")
message(STATUS "Using IniPP from submodule...")
add_subdirectory("${INIPP_SOURCE_DIR}")

## JSON
set(JSON_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/Source/ThirdParty/JSON")
message(STATUS "Using JSON from submodule...")
add_subdirectory("${JSON_SOURCE_DIR}")

# Now finally export our library file.
## Setup the project and set a few variables
project(EnigmaFix)
add_library(EnigmaFix SHARED
        Source/dllmain.cpp
        Source/Localization/Localization.cpp
        Source/Localization/Localization.h
        Source/Managers/ConfigManager.cpp
        Source/Managers/ConfigManager.h
        Source/Managers/FramerateManager.cpp
        Source/Managers/FramerateManager.h
        Source/Managers/HookManager.cpp
        Source/Managers/HookManager.h
        Source/Managers/PatchManager.cpp
        Source/Managers/PatchManager.h
        Source/Managers/PluginManager.cpp
        Source/Managers/PluginManager.h
        Source/Managers/RenderManager.cpp
        Source/Managers/RenderManager.h
        Source/Managers/UIManager.cpp
        Source/Managers/UIManager.h
        Source/Settings/PlayerSettings.cpp
        Source/Settings/PlayerSettings.h
        Source/Plugins/Plugin_DERQ.cpp
        Source/Plugins/Plugin_DERQ.h
        Source/Utilities/UITextureLoader.cpp
        Source/Utilities/UITextureLoader.h
        Source/Utilities/DXHelper.h
        Source/Utilities/DXEnums.h
        Source/Utilities/MemoryHelper.cpp
        Source/Managers/LogManager.cpp
        Source/Managers/LogManager.h
        Source/Utilities/DisplayHelper.h
        Source/Utilities/DisplayHelper.cpp
        Source/Utilities/MemoryHelper.h
        Source/Utilities/JsonHelper.cpp
        Source/Utilities/JsonHelper.h
        Source/Utilities/FOVHelper.cpp
        Source/Utilities/FOVHelper.h
        Source/Utilities/ColorHelper.cpp
        Source/Utilities/ColorHelper.h
        ${RESOURCE_FILE}
)

## STB (We need to set this after EnigmaFix due to some MSVC discrepancies).
set(STB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/Source/ThirdParty/STB")
message(STATUS "Using STB from submodule...")
if(MINGW)
    add_library(stb STATIC ${STB_SOURCE_DIR}/stb_image.h)
    set_target_properties(stb PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(EnigmaFix PRIVATE stb)
    add_dependencies(EnigmaFix stb)
else() # MSVC
    target_include_directories(EnigmaFix PRIVATE "${STB_SOURCE_DIR}")
endif()

## Add the dependencies we set up previously.
# NOTE: Add "" to the "add_dependencies" section when you finally get that shit working.
add_dependencies(EnigmaFix imgui kiero safetyhook inipp nlohmann_json spdlog minhook)
target_link_libraries(EnigmaFix PRIVATE d3dcompiler imgui kiero safetyhook inipp nlohmann_json spdlog minhook)
#target_link_libraries(EnigmaFix d3dcompiler imgui kiero inipp ${Boost_LIBRARIES})
## Set up our linker flags for compiling.
if(MINGW)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -static -municode -Wl,--file-alignment=4096")
elseif(MSVC)
    set(CMAKE_SHARED_LINKER_FLAGS "") # MSVC doesn’t need GCC flags
endif()
# Renames our output to an ASI extension for use with Ultimate ASI Loader.
set_target_properties(EnigmaFix PROPERTIES PREFIX "" OUTPUT_NAME EnigmaFix SUFFIX ".asi")
## Now we delete any miscellaneous .a static library files for release builds, as we don't need them.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove $<TARGET_FILE_DIR:${PROJECT_NAME}>/*.a)
endif()