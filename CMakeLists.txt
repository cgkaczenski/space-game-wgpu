#https://github.com/meemknight/cmakeSetup
#Version 1.0.0

cmake_minimum_required(VERSION 3.16)


#set this to true to ship the game!
#! ! ! ! ! ! !
#DELETE THE OUT FOLDER AFTER CHANGING THIS BECAUSE VISUAL STUDIO DOESN'T SEEM TO RECOGNIZE THIS CHANGE AND REBUILD!
option(PRODUCTION_BUILD "Make this a production build" OFF)
#DELETE THE OUT FOLDER AFTER CHANGING THIS BECAUSE VISUAL STUDIO DOESN'T SEEM TO RECOGNIZE THIS CHANGE AND REBUILD!

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDebug$<$<CONFIG:Debug>:Debug>")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Release>:Release>")

project(spaceGame)

# Set C++ standard globally
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# clangd / the editor's C++ language server read this to get the same
# include paths CMake passes the compiler (FetchContent WebGPU headers
# live under build/_deps, which is gitignored and otherwise invisible).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(thirdparty/glfw-3.4)			#window oppener
add_subdirectory(thirdparty/stb_image)			#loading immaged
add_subdirectory(thirdparty/stb_truetype)		#loading ttf files
#add_subdirectory(thirdparty/enet-1.3.17)		#networking
add_subdirectory(thirdparty/raudio)				#audio
add_subdirectory(thirdparty/glm)				#math
add_subdirectory(thirdparty/imgui-docking)		#ui
#----------V-----------------------V------------#my libraries												
add_subdirectory(thirdparty/safeSave)			#saving data, designed for gamedev. It also makes backups so the player won't loose saves.
add_subdirectory(thirdparty/profilerLib)		#profiling, just a simpe library for measuring elapsed time
add_subdirectory(thirdparty/glui)				#layout math (Frame, Box); the rest of glui was a gl2d UI and is gone


# The sprite shader, compiled into the library rather than loaded from disk.
# A library cannot reach into an application's resource layout, and
# RESOURCES_PATH is the application's. Generated as a raw string literal so the
# WGSL needs no escaping and resources/shaders/quad.wgsl stays an ordinary,
# editable, syntax-highlighted file.
#
# The cost is real: the shader is no longer swappable without a rebuild.
# CMAKE_CONFIGURE_DEPENDS at least makes editing it re-run this step.
#
# The contents follow R"WGSL( with no newline between, deliberately: a leading
# newline would make string line 1 the file's line 0 and every WGSL compile
# error would report one line further down than the file it came from.
set(QUAD_WGSL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/quad.wgsl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${QUAD_WGSL_SRC}")
file(READ "${QUAD_WGSL_SRC}" QUAD_WGSL_CONTENTS)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/render/quadShaderSource.h"
"#pragma once
// Generated from resources/shaders/quad.wgsl by CMake. Do not edit.
namespace render
{
\tinline const char *quadShaderWGSL = R\"WGSL(${QUAD_WGSL_CONTENTS})WGSL\";
}
")

# Define MY_SOURCES to be a list of all the source files for my game.
# src/render is not among them: it is the wgpu2d library target below, and the
# whole point of that target is that these sources cannot see the game's.
file(GLOB_RECURSE MY_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER MY_SOURCES EXCLUDE REGEX "/src/render/")


add_executable("${CMAKE_PROJECT_NAME}")

set_property(TARGET "${CMAKE_PROJECT_NAME}" PROPERTY CXX_STANDARD 17)



if(PRODUCTION_BUILD)
	# setup the ASSETS_PATH macro to be in the root folder of your exe
	target_compile_definitions("${CMAKE_PROJECT_NAME}" PUBLIC RESOURCES_PATH="./resources/") 

	# remove the option to debug asserts.
	target_compile_definitions("${CMAKE_PROJECT_NAME}" PUBLIC PRODUCTION_BUILD=1) 

else()
	# This is useful to get an ASSETS_PATH in your IDE during development
	target_compile_definitions("${CMAKE_PROJECT_NAME}" PUBLIC RESOURCES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/resources/")
	target_compile_definitions("${CMAKE_PROJECT_NAME}" PUBLIC PRODUCTION_BUILD=0) 

endif()

target_sources("${CMAKE_PROJECT_NAME}" PRIVATE ${MY_SOURCES} )


if(MSVC) # If using the VS compiler...

	target_compile_definitions("${CMAKE_PROJECT_NAME}" PUBLIC _CRT_SECURE_NO_WARNINGS)

	set_target_properties("${CMAKE_PROJECT_NAME}" PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup") #no console
	
	set_property(TARGET "${CMAKE_PROJECT_NAME}" PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDebug<$<CONFIG:Debug>:Debug>")
	set_property(TARGET "${CMAKE_PROJECT_NAME}" PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Release>:Release>")

endif()

target_include_directories("${CMAKE_PROJECT_NAME}" PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated/")
target_include_directories("${CMAKE_PROJECT_NAME}" PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/")
target_include_directories("${CMAKE_PROJECT_NAME}" PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/gameLayer/")
target_include_directories("${CMAKE_PROJECT_NAME}" PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/")


#enet not working yet on linux for some reason
target_link_libraries("${CMAKE_PROJECT_NAME}" PRIVATE glm glfw
	stb_image stb_truetype raudio imgui safeSave profilerLib glui)

# WebGPU-distribution fetches a prebuilt wgpu-native release and exports the
# `webgpu` target plus the bundled webgpu.hpp. glfw3webgpu turns a GLFW
# window into a WGPUSurface; it assumes the `glfw` and `webgpu` targets exist
# and, on macOS, compiles itself as Objective-C and links Cocoa/QuartzCore.
# Both are pinned to tags that were tested together (GLFW 3.4 line).
include(FetchContent)
set(WEBGPU_BACKEND "WGPU" CACHE STRING "WebGPU implementation to use" FORCE)
set(WEBGPU_BUILD_FROM_SOURCE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(webgpu-distribution
	GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
	GIT_TAG        v0.3.0-gamma
)
FetchContent_Declare(glfw3webgpu
	GIT_REPOSITORY https://github.com/eliemichel/glfw3webgpu
	GIT_TAG        v1.3.0-alpha
)
FetchContent_MakeAvailable(webgpu-distribution glfw3webgpu)

target_link_libraries("${CMAKE_PROJECT_NAME}" PRIVATE webgpu glfw3webgpu)
target_copy_webgpu_binaries("${CMAKE_PROJECT_NAME}")

# ---------------------------------------------------------------------------
# wgpu2d: the 2D drawing library.
#
# Defined after FetchContent because it links the `webgpu` target. Its sources
# are everything under src/render and nothing else, which is what turns the
# inward boundary -- render/ must not use gameLayer/ -- from a rule in
# AGENTS.md into a link error.
#
# Note what this does NOT catch: the game including render/wgpuFrame.h still
# compiles, because headers do not respect target boundaries. That direction
# stays a rule.
#
# It links no window library. wgpuInit takes a WGPUSurface the application
# already made, so nothing here knows GLFW exists.
# ---------------------------------------------------------------------------
add_library(wgpu2d STATIC
	"${CMAKE_CURRENT_SOURCE_DIR}/src/render/wgpuContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/render/layerEffect.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/render/webgpuImpl.cpp"
)
set_property(TARGET wgpu2d PROPERTY CXX_STANDARD 17)

# PUBLIC: wgpu2d.h includes glm, wgpuFrame.h includes webgpu.hpp, and the
# generated shader header is included by wgpuContext.cpp from the same
# directory the consumers search. stb_image is PRIVATE -- Texture::loadFromFile
# uses it, no header exposes it.
target_include_directories(wgpu2d PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/include/"
	"${CMAKE_CURRENT_BINARY_DIR}/generated/")
target_link_libraries(wgpu2d PUBLIC webgpu glm PRIVATE stb_image)
# No glfw, no glfw3webgpu: the application creates the surface and hands it in.
# That is what makes this target usable by a project that is not on GLFW.

target_link_libraries("${CMAKE_PROJECT_NAME}" PRIVATE wgpu2d)
# GLFW's header includes the system OpenGL header by default. This project
# never needs it.
target_compile_definitions("${CMAKE_PROJECT_NAME}" PUBLIC GLFW_INCLUDE_NONE)

if(APPLE)
	# Objective-C++ helper that pins the CAMetalLayer color space to sRGB
	# (see include/platform/wgpuMetalLayer.h). The .mm is not in the *.cpp
	# glob. Compiled through the C++ toolchain with -x objective-c++ (as
	# glfw3webgpu does for its .c file) rather than enable_language(OBJCXX),
	# which, called this late, breaks the rules already generated for GLFW.
	set(WGPU_METAL_LAYER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/wgpuMetalLayer.mm")
	set_source_files_properties("${WGPU_METAL_LAYER_SRC}" PROPERTIES
		LANGUAGE CXX
		COMPILE_OPTIONS "-x;objective-c++")
	target_sources("${CMAKE_PROJECT_NAME}" PRIVATE "${WGPU_METAL_LAYER_SRC}")
	target_link_libraries("${CMAKE_PROJECT_NAME}" PRIVATE "-framework QuartzCore" "-framework CoreGraphics")
endif()
