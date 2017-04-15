# This file is part of vos-installer.
# Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
# This file is licensed under the Apache License Version 2.0.

cmake_minimum_required(VERSION 3.4)
project(vos-installer)

#add_subdirectory(cli)

find_package(PkgConfig REQUIRED)

set(CMAKE_BUILD_TYPE Debug)

add_executable(vos-install-gui
	main.c
	page-00-home.c
	page-01-drive-select.c
	sd-utils.c
)
pkg_check_modules(LIBUDEV REQUIRED libudev)
pkg_check_modules(CLUTTER REQUIRED clutter-1.0>=1.26)
target_include_directories(vos-install-gui PRIVATE
	.
	${CLUTTER_INCLUDE_DIRS}
	${LIBUDEV_INCLUDE_DIRS}
)
target_link_libraries(vos-install-gui
	cmk
	${CLUTTER_LIBRARIES}
	${LIBUDEV_LIBRARIES}
)
