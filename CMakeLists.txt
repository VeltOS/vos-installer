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
	page-01b-boot-select.c
	page-02-profile.c
	page-03-complete.c
	sd-utils.c
)

#add_custom_target(build_cmk
#	WORKING_DIRECTORY /home/aidan/projects/cmk/build
#	COMMAND make
#	COMMAND sudo make install > /dev/null
#)
#
#add_dependencies(vos-install-gui build_cmk)


find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUDEV REQUIRED libudev)
pkg_check_modules(CMK REQUIRED libcmk>=0.4)

target_include_directories(vos-install-gui PRIVATE
	${CMK_INCLUDE_DIRS}
	${LIBUDEV_INCLUDE_DIRS}
)
target_link_libraries(vos-install-gui
	SegFault
	m
	${CMK_LIBRARIES}
	${LIBUDEV_LIBRARIES}
)

add_subdirectory(cli)
