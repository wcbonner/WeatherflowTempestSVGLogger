﻿# CMakeList.txt : CMake project for WeatherflowTempestSVGLogger, include source and define project specific logic here.

cmake_minimum_required (VERSION 3.12.0)

if (POLICY CMP0115)
  cmake_policy(SET CMP0115 NEW)
endif()

project(
	WeatherflowTempestSVGLogger
	VERSION 1.20241004.0
	DESCRIPTION "Listen and log Weatherflow Tempest Weather Station UDP Packets"
	HOMEPAGE_URL https://github.com/wcbonner/WeatherflowTempestSVGLogger
)

configure_file(
	weatherflowtempestsvglogger-version.h.in
	weatherflowtempestsvglogger-version.h
)

# Add source to this project's executable.
add_executable(
	${PROJECT_NAME}
	weatherflowtempestsvglogger.cpp
	weatherflowtempestsvglogger-version.h
	wimiso8601.cpp
	wimiso8601.h
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC "${PROJECT_BINARY_DIR}" ${EXTRA_INCLUDES}
)

find_package(jsoncpp REQUIRED)

target_link_libraries(
	${PROJECT_NAME}
	-lstdc++fs
	jsoncpp_lib
)

include(CTest)
add_test(
	NAME Runs
	COMMAND ${PROJECT_NAME} --help
)

install(
	TARGETS ${PROJECT_NAME}
	DESTINATION bin
	RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
	LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)

install(
	FILES weatherflowtempestsvglogger.service
	DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system
	COMPONENT "weatherflowtempestsvglogger"
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_CONTACT "wcbonner@users.noreply.github.com")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "William C Bonner <${CPACK_PACKAGE_CONTACT}>")
include(InstallRequiredSystemLibraries)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION custom)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/postinst" "${CMAKE_CURRENT_SOURCE_DIR}/prerm" "${CMAKE_CURRENT_SOURCE_DIR}/postrm")
set(CPACK_STRIP_FILES YES)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS YES)
message(STATUS "Components to pack: ${CPACK_COMPONENTS_ALL}")
include(CPack)
