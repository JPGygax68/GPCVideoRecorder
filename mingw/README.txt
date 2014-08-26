This directory contains the LibX264 header and library files, built with MinGW, plus two library files LibX264 depends on.

include/
  x264.h          The header file
  x264_config.h   The configuration the library was built for.
    
lib/  
  libx264.a       LibX264 encoder/decoder library
  libgcc.a        Gnu C Compiler (GCC) low-level library routines
  libmingwex.a    C99 compatibility library


If you have MinGW installed on your system, it is of course better to build LibX264 yourself, find the other two library files in the MinGW installation tree, and make all of them available by setting the INCLUDE and LIB environment variable accordingly.

Note: the CMakeLists.txt file contains special configuration to deal with the cultural difference between library files ending in ".a" and ".lib", so, provided that LIB is set correctly, CMake *will* be able to use them.