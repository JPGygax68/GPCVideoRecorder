cmake_minimum_required(VERSION 3.0)

#------------------------------------------------
# Project + basics
#

project(libGPCVideoRecorder)

set(LSApplication_MAJOR_VERSION 0)
set(LSApplication_MINOR_VERSION 1)
set(LSApplication_PATCH_VERSION 0)
set(LSApplication_VERSION ${${PROJECT_NAME}_MAJOR_VERSION}.${${PROJECT_NAME}_MINOR_VERSION}.${${PROJECT_NAME}_PATCH_VERSION})

#------------------------------------------------
# Special settings
#

if (0)
# Force static runtime libraries
FOREACH(flag
  CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
  CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG_INIT
  CMAKE_CXX_FLAGS_RELEASE  CMAKE_CXX_FLAGS_RELWITHDEBINFO
  CMAKE_CXX_FLAGS_DEBUG  CMAKE_CXX_FLAGS_DEBUG_INIT)
  STRING(REPLACE "/MD"  "/MT" "${flag}" "${${flag}}")
  SET("${flag}" "${${flag}} /EHsc")
ENDFOREACH()
endif()
 
#-----------------------------------------------
# Installation paths
#

# Offer the user the choice of overriding the installation directories
set(INSTALL_LIB_DIR lib CACHE PATH "Installation directory for libraries")
#set(INSTALL_BIN_DIR bin CACHE PATH "Installation directory for executables")
set(INSTALL_INCLUDE_DIR include CACHE PATH "Installation directory for header files")
if(WIN32 AND NOT CYGWIN)
	set(DEF_INSTALL_CMAKE_DIR CMake)
else()
	set(DEF_INSTALL_CMAKE_DIR lib/CMake/${PROJECT_NAME})
endif()
set(INSTALL_CMAKE_DIR ${DEF_INSTALL_CMAKE_DIR} CACHE PATH "Installation directory for CMake files")
 
# Make relative paths absolute (needed later on)
foreach(p LIB BIN INCLUDE CMAKE)
	set(var INSTALL_${p}_DIR)
	if(NOT IS_ABSOLUTE "${${var}}")
		set(${var} "${CMAKE_INSTALL_PREFIX}/${${var}}")
	endif()
endforeach()

#------------------------------------------------
# Main target: static library
#

set(PUBLIC_HEADERS "VideoRecorder.hpp")

set(PUBLIC_HEADER_PATHS "")
foreach (HEADER ${PUBLIC_HEADERS})
	set(PUBLIC_HEADER_FULL_PATHS ${PUBLIC_HEADER_FULL_PATHS} "${HEADER}")
endforeach()

add_library( ${PROJECT_NAME} STATIC
  src/VideoRecorder.cpp
  include/gpc/VideoRecorder.hpp
)

target_include_directories(${PROJECT_NAME} PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)

set_target_properties( ${PROJECT_NAME}
  PROPERTIES
    DEBUG_POSTFIX D
)

#------------------------------------------------
# Libraries
#

# FFMPEG library

if (CMAKE_SIZEOF_VOID_P MATCHES 8)
	set( PROJECT_ARCH "x86_64" )
else(CMAKE_SIZEOF_VOID_P MATCHES 8)
	set( PROJECT_ARCH "x86" )
endif(CMAKE_SIZEOF_VOID_P MATCHES 8)

set(FFmpeg_ROOT "" CACHE PATH "Root of FFmpeg library (libAV)")

foreach (lib "avformat" "avcodec" "avutil" "swscale")

	# Library files
	find_library(${lib}_LIB_RELEASE "${lib}" PATHS ${FFmpeg_ROOT}/lib/${PROJECT_ARCH} ${FFmpeg_ROOT}/lib)
	if (NOT ${lib}_LIB_RELEASE)
		message(FATAL_ERROR "Could not find release version of library \"${lib}\"")
	endif()
	find_library(${lib}_LIB_DEBUG "${lib}" PATH ${FFmpeg_ROOT}/lib/${PROJECT_ARCH} ${FFmpeg_ROOT}/lib)
	if (NOT ${lib}_LIB_DEBUG)
		message(FATAL_ERROR "Could not find debug version of library \"${lib}\"")
	endif()
	target_link_libraries(${PROJECT_NAME} PUBLIC 
		$<$<CONFIG:Release>:${${lib}_LIB_RELEASE}>
		$<$<CONFIG:Debug>:${${lib}_LIB_DEBUG}>
	)

	# Header file
	find_path(${lib}_INCLUDE "lib${lib}/${lib}.h" PATHS ${FFmpeg_ROOT}/include)
	if (NOT ${lib}_INCLUDE)
		message(FATAL_ERROR "Could not find header file of library \"${lib}\"")
	endif()
	target_include_directories(${PROJECT_NAME} PRIVATE ${${lib}_INCLUDE})
	
endforeach()

#------------------------------------------------
# Exporting
#

# Put our target into the build tree export set
export(TARGETS ${PROJECT_NAME} FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-targets.cmake)

# Export the package for use directly from the build tree
# (registers the build tree with a global CMake registry)
export(PACKAGE ${PROJECT_NAME})

#------------------------------------------------
# Config files

# Create the <project>-config.cmake and <project>-config-version files

# ... for the build tree
set(CONF_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include" "${PROJECT_BINARY_DIR}")
get_target_property(CONF_LINK_FLAGS ${PROJECT_NAME} LINK_FLAGS)
configure_file(${PROJECT_NAME}-config.cmake.in "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake" @ONLY)

# ... for the install tree
file(RELATIVE_PATH REL_INCLUDE_DIR "${INSTALL_CMAKE_DIR}" "${INSTALL_INCLUDE_DIR}")
set(CONF_INCLUDE_DIRS "\${${PROJECT_NAME}_CMAKE_DIR}/${REL_INCLUDE_DIR}")
configure_file(${PROJECT_NAME}-config.cmake.in "${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${PROJECT_NAME}-config.cmake" @ONLY)

# ... for both
configure_file(${PROJECT_NAME}-config-version.cmake.in "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake" @ONLY)

#------------------------------------------------
# INSTALLATION
#

install(TARGETS ${PROJECT_NAME}
	EXPORT ${PROJECT_NAME}-targets
	ARCHIVE			DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
	PUBLIC_HEADER	DESTINATION "${INSTALL_INCLUDE_DIR}/${PROJECT_NAME}/"
)

#install(FILES ${PUBLIC_HEADER_PATHS} DESTINATION include)

install(DIRECTORY include/gpc DESTINATION "${INSTALL_INCLUDE_DIR}")

# Install the ..config.cmake and ..config-version.cmake files
install(FILES
  "${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${PROJECT_NAME}-config.cmake"
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake"
  DESTINATION "${INSTALL_CMAKE_DIR}"
)

 # Install the export set for use with the install-tree
install(EXPORT ${PROJECT_NAME}-targets DESTINATION "${INSTALL_CMAKE_DIR}" COMPONENT dev)

