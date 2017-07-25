# This file is part of vos-installer.
# Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
# This file is licensed under the Apache License Version 2.0.

cmake_minimum_required(VERSION 3.4)
project(vos-installer)
set(CMAKE_BUILD_TYPE Debug)

add_executable(vos-install-gui
	main.c
	page-00-home.c
	page-01-drive-select.c
	page-02-profile.c
	page-03-complete.c
	sd-utils.c
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUDEV REQUIRED libudev)
pkg_check_modules(CMK REQUIRED libcmk>=0.1.0)

target_include_directories(vos-install-gui PRIVATE
	${CMK_INCLUDE_DIRS}
	#/home/aidan/projects/cmk/src
	${LIBUDEV_INCLUDE_DIRS}
)
target_link_libraries(vos-install-gui
	SegFault
	${CMK_LIBRARIES}
	#/home/aidan/projects/cmk/build/libcmk.so
	${LIBUDEV_LIBRARIES}
	m
)

#add_subdirectory(/home/aidan/projects/cmk /home/aidan/projects/cmk/build)
add_subdirectory(cli)
#add_dependencies(vos-install-gui vos-install-cli cmk)
