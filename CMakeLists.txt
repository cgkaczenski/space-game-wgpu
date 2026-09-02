#https://github.com/meemknight/cmakeSetup
#Version 1.0.0

cmake_minimum_required(VERSION 3.16)


#set this to true to ship the game!
#! ! ! ! ! ! !
#DELETE THE OUT FOLDER AFTER CHANGING THIS BECAUSE VISUAL STUDIO DOESN'T SEEM TO RECOGNIZE THIS CHANGE AND REBUILD!
option(PRODUCTION_BUILD "Make this a production build" OFF)
#DELETE THE OUT FOLDER AFTER CHANGING THIS BECAUSE VISUAL STUDIO DOESN'T SEEM TO RECOGNIZE THIS CHANGE AND REBUILD!

# Render with WebGPU (wgpu-native on Metal) instead of OpenGL. OFF keeps the
# original OpenGL/gl2d path exactly as it was. Use a separate build directory
# per setting, e.g. build/ for OpenGL and build-webgpu/ for WebGPU.
option(RENDERER_WEBGPU "Use the WebGPU render path instead of OpenGL" OFF)


set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDebug$<$<CONFIG:Debug>:Debug>")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Release>:Release>")

project(spaceGame)

# Set C++ standard globally
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(thirdparty/glfw-3.4)			#window oppener
add_subdirectory(thirdparty/glad)				#opengl loader
add_subdirectory(thirdparty/stb_image)			#loading immaged
add_subdirectory(thirdparty/stb_truetype)		#loading ttf files
#add_subdirectory(thirdparty/enet-1.3.17)		#networking
add_subdirectory(thirdparty/raudio)				#audio
add_subdirectory(thirdparty/glm)				#math
add_subdirectory(thirdparty/imgui-docking)		#ui
#----------V-----------------------V------------#my libraries												
add_subdirectory(thirdparty/safeSave)			#saving data, designed for gamedev. It also makes backups so the player won't loose saves.
add_subdirectory(thirdparty/profilerLib)		#profiling, just a simpe library for measuring elapsed time
add_subdirectory(thirdparty/gl2d)				#2D rendering library
add_subdirectory(thirdparty/glui)				#ui library, usefull for making game menus


# Define MY_SOURCES to be a list of all the source files for my game 
file(GLOB_RECURSE MY_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")


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

target_include_directories("${CMAKE_PROJECT_NAME}" PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/")
target_include_directories("${CMAKE_PROJECT_NAME}" PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/gameLayer/")
target_include_directories("${CMAKE_PROJECT_NAME}" PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/")


#target_link_libraries("${CMAKE_PROJECT_NAME}" PRIVATE glm glfw 
#	glad stb_image stb_truetype gl2d raudio imgui safeSave profilerLib enet glui)

#enet not working yet on linux for some reason
target_link_libraries("${CMAKE_PROJECT_NAME}" PRIVATE glm glfw 
	glad stb_image stb_truetype gl2d raudio imgui safeSave profilerLib glui)


if(RENDERER_WEBGPU)
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
	target_compile_definitions("${CMAKE_PROJECT_NAME}" PUBLIC RENDERER_WEBGPU=1)
else()
	target_compile_definitions("${CMAKE_PROJECT_NAME}" PUBLIC RENDERER_WEBGPU=0)
endif()
