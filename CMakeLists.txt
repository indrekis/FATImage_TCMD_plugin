# Configure by:
# Using MSVC 2019 and create 64-bit plugin:
#  cmake -A x64   .. -G"Visual Studio 16" 	
# Using MSVC 2019 and create 32-bit plugin:
#  cmake -A Win32 .. -G"Visual Studio 16"
#
# To enable FLTK usage:
#  cmake -A x64   .. -G"Visual Studio 16" -DFLTK_ENABLED_EXPERIMENTAL=1
#  cmake -A Win32 .. -G"Visual Studio 16" -DFLTK_ENABLED_EXPERIMENTAL=1
#
# Build by:
#  cmake --build . --config Release
# or any of:
#  cmake --build . --config [Debug|RelWithDebInfo|MinSizeRel]

cmake_minimum_required(VERSION 3.15)
project(l17_fixed_string)

set(CMAKE_CXX_STANDARD 20)

set(SOURCE_FILES FAT_definitions.cpp  FAT_definitions.h  IMGwcx.cpp  minimal_fixed_string.h  resource.h  sysio_winapi.cpp  sysio_winapi.h wcxhead.h main_resources.rc
string_tools.cpp string_tools.h plugin_config.cpp plugin_config.h )

add_library(IMGwcx64 SHARED ${SOURCE_FILES} )
set_target_properties(IMGwcx64 PROPERTIES OUTPUT_NAME img)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set_target_properties(IMGwcx64 PROPERTIES SUFFIX .wcx64 PREFIX "") 
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set_target_properties(IMGwcx64 PROPERTIES SUFFIX .wcx PREFIX "") 	
endif()

if( DEFINED FLTK_ENABLED_EXPERIMENTAL)
find_package(FLTK CONFIG)

if( FLTK_FOUND )
target_compile_definitions(IMGwcx64 PRIVATE -DFLTK_ENABLED_EXPERIMENTAL=1)
target_include_directories(IMGwcx64 PRIVATE ${FLTK_INCLUDE_DIR})
target_link_libraries(IMGwcx64 PRIVATE fltk fltk_gl fltk_forms fltk_images)
endif()

endif()

# https://www.ghisler.ch/wiki/index.php?title=Plugins_Automated_Installation