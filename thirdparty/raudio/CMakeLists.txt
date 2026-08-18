cmake_minimum_required(VERSION 3.5)
project(raudio C)

add_library(raudio)
target_sources(raudio PRIVATE src/raudio.c)
target_include_directories(raudio PUBLIC include)

# miniaudio's default dlopen("CoreAudio.framework/CoreAudio") fails on macOS.
# Link the frameworks directly so InitAudioDevice can open a real playback device.
if(APPLE)
	target_compile_definitions(raudio PRIVATE MA_NO_RUNTIME_LINKING)
	target_link_libraries(raudio PUBLIC
		"-framework CoreAudio"
		"-framework AudioToolbox"
		"-framework AudioUnit"
		"-framework CoreFoundation"
	)
endif()
