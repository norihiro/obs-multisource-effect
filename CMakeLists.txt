cmake_minimum_required(VERSION 3.12)

project(obs-multisource-effect VERSION 0.2.0)

set(PLUGIN_AUTHOR "Norihiro Kamae")

set(MACOS_BUNDLEID "net.nagater.obs-multisource-effect")
set(MACOS_PACKAGE_UUID "6B80E1DD-DD89-4E4C-9AFF-7B3100895C93")
set(MACOS_INSTALLER_UUID "A6B6A884-A6D9-4965-9079-1D5501A8E091")

set(LINUX_MAINTAINER_EMAIL "norihiro@nagater.net")

# In case you need C++
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
include(cmake/ObsPluginHelpers.cmake)

configure_file(
	src/plugin-macros.h.in
	../src/plugin-macros.generated.h
)
configure_file(
	installer/installer-Windows.iss.in
	../installer/installer-Windows.generated.iss
)

configure_file(
	ci/ci_includes.sh.in
	../ci/ci_includes.generated.sh
)
configure_file(
	ci/ci_includes.cmd.in
	../ci/ci_includes.generated.cmd
)

set(PLUGIN_SOURCES
	src/multisource-effect.c
	src/source_list.cc
)

add_library(${CMAKE_PROJECT_NAME} MODULE ${PLUGIN_SOURCES})

target_link_libraries(${CMAKE_PROJECT_NAME}
	OBS::libobs
	OBS::obs-frontend-api
)

if(OS_WINDOWS)
	# Enable Multicore Builds and disable FH4 (to not depend on VCRUNTIME140_1.DLL when building with VS2019)
	if (MSVC)
		add_definitions(/MP /d2FH4-)
	endif()

	target_link_libraries(${CMAKE_PROJECT_NAME} OBS::w32-pthreads)
endif()

if(OS_LINUX)
	target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

if(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -fvisibility=default")

	set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PREFIX "")
	set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
	set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
	set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")
endif()

setup_plugin_target(${CMAKE_PROJECT_NAME})

configure_file(installer/installer-macOS.pkgproj.in installer-macOS.generated.pkgproj)
