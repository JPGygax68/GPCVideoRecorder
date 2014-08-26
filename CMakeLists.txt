cmake_minimum_required(VERSION 3.0)

#------------------------------------------------
# Project + basics
#

project(GPCVideoRecorder)

set(${PROJECT_NAME}_MAJOR_VERSION 0)
set(${PROJECT_NAME}_MINOR_VERSION 1)
set(${PROJECT_NAME}_PATCH_VERSION 0)
set(${PROJECT_NAME}_VERSION ${${PROJECT_NAME}_MAJOR_VERSION}.${${PROJECT_NAME}_MINOR_VERSION}.${${PROJECT_NAME}_PATCH_VERSION})

set(${PROJECT_NAME}_DEFINITIONS "-DUSE_LIBX264")

#------------------------------------------------
# Special settings
#

# Force static runtime libraries

FOREACH(flag
  CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
  CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG_INIT
  CMAKE_CXX_FLAGS_RELEASE  CMAKE_CXX_FLAGS_RELWITHDEBINFO
  CMAKE_CXX_FLAGS_DEBUG  CMAKE_CXX_FLAGS_DEBUG_INIT)
  STRING(REPLACE "/MD"  "/MT" "${flag}" "${${flag}}")
  SET("${flag}" "${${flag}} /EHsc")
ENDFOREACH()
 
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
  src/VideoRecorder.cpp			# src/X264Encoder.cpp
  include/gpc/VideoRecorder.hpp # src/X264Encoder.h
)

target_include_directories(${PROJECT_NAME} PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)

target_compile_definitions(${PROJECT_NAME} PUBLIC ${${PROJECT_NAME}_DEFINITIONS})

set_target_properties( ${PROJECT_NAME}
  PROPERTIES
    DEBUG_POSTFIX D
)

#------------------------------------------------
# Libraries
#

# For libraries originating from the other side of the cultural divide:
list(APPEND CMAKE_FIND_LIBRARY_SUFFIXES ".a")
list(REMOVE_DUPLICATES CMAKE_FIND_LIBRARY_SUFFIXES)

# FFMPEG library

# This search only works if the .lib and .h files have been installed in "sensible" locations
# below one of the directories listed in the env variable CMAKE_PREFIX_PATH

find_library(avformat_LIB avformat)
find_path(avformat_INCLUDE libavformat/avformat.h)
find_library(avcodec_LIB avcodec)
find_path(avcodec_INCLUDE libavcodec/avcodec.h)
find_library(avutil_LIB avutil)
find_path(avutil_INCLUDE libavutil/avutil.h)
find_library(swscale_LIB swscale)
find_path(swscale_INCLUDE libswscale/swscale.h)

target_link_libraries(${PROJECT_NAME} ${avformat_LIB} ${avcodec_LIB} ${avutil_LIB} ${swscale_LIB} ${x264_LIB})
target_include_directories(${PROJECT_NAME} PUBLIC ${avformat_INCLUDE} ${avcodec_INCLUDE} ${avutil_INCLUDE} ${swscale_INCLUDE} ${x264_INCLUDE})
target_compile_definitions(${PROJECT_NAME} PRIVATE __STDC_CONSTANT_MACROS)

# Requires disabling of "SAFESEH"
get_target_property(link_flags ${PROJECT_NAME} LINK_FLAGS)
if (NOT link_flags)
  set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/SAFESEH:NO")
else()
  set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "${link_flags} /SAFESEH:NO")
endif()
get_target_property(link_flags ${PROJECT_NAME} LINK_FLAGS)

# LibX264 library + dependencies

find_library(x264_LIB libx264)
find_path(x264_INCLUDE x264.h)

find_library(gcc_LIB libgcc)
target_link_libraries(${PROJECT_NAME} ${gcc_LIB})

find_library(mingwex_LIB libmingwex)
target_link_libraries(${PROJECT_NAME} ${mingwex_LIB})

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

