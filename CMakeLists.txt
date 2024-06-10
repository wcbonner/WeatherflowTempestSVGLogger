# CMakeList.txt : CMake project for WeatherflowTempestSVGLogger, include source and define project specific logic here.

cmake_minimum_required (VERSION 3.12.0)

project ("WeatherflowTempestSVGLogger"
    VERSION 1.20240609.0
    DESCRIPTION "Listen and log Weatherflow Tempest Weather Station UDP Packets"
    HOMEPAGE_URL https://github.com/wcbonner/WeatherflowTempestSVGLogger
	)

configure_file(weatherflowtempestsvglogger-version.h.in weatherflowtempestsvglogger-version.h)

# Add source to this project's executable.
add_executable (WeatherflowTempestSVGLogger 
	"weatherflowtempestsvglogger.cpp"
	"wimiso8601.cpp"
	"wimiso8601.h"
	)

target_link_libraries(WeatherflowTempestSVGLogger 
	-lstdc++fs
	)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET WeatherflowTempestSVGLogger PROPERTY CXX_STANDARD 17)
endif()

include(CTest)
add_test(NAME WeatherflowTempestSVGLogger COMMAND WeatherflowTempestSVGLogger --help)

install(TARGETS WeatherflowTempestSVGLogger
    DESTINATION bin
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_CONTACT "wcbonner@users.noreply.github.com")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "William C Bonner <${CPACK_PACKAGE_CONTACT}>")
include(InstallRequiredSystemLibraries)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_RELEASE ${CMAKE_PROJECT_VERSION_PATCH})
set(CPACK_DEBIAN_PACKAGE_SECTION custom)
set(CPACK_STRIP_FILES YES)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS YES)
message(STATUS "Components to pack: ${CPACK_COMPONENTS_ALL}")
include(CPack)
