# This file is part of vos-installer.
# Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
# This file is licensed under the Apache License Version 2.0.

add_executable(vos-install-cli
	main.c
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(LIBUDEV REQUIRED libudev)

target_include_directories(vos-install-cli PRIVATE
	${GLIB_INCLUDE_DIRS}
	${GIO_INCLUDE_DIRS}
	${LIBUDEV_INCLUDE_DIRS}
)
target_link_libraries(vos-install-cli
	SegFault
	pthread
	${GLIB_LIBRARIES}
	${GIO_LIBRARIES}
	${LIBUDEV_LIBRARIES}
)
