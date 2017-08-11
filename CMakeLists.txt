# This file is part of vos-installer.
# Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
# This file is licensed under the Apache License Version 2.0.

cmake_minimum_required(VERSION 3.4)
project(vos-installer)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "-Wall -Wextra -DUNUSED=G_GNUC_UNUSED")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O3")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "default install path" FORCE)
endif()

add_executable(vos-install-gui
	main.c
	page-00-home.c
	page-01-drive-select.c
	page-01b-boot-select.c
	page-02-profile.c
	page-03-complete.c
	sd-utils.c
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUDEV REQUIRED libudev)
pkg_check_modules(CMK REQUIRED libcmk>=0.5)

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

install(TARGETS vos-install-gui DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
