# cmake -A x64 .. -G"Visual Studio 16"
# cmake -A Win32 .. -G"Visual Studio 16"
# cmake --build . --config Release

cmake_minimum_required(VERSION 3.15)
project(l17_fixed_string)

set(CMAKE_CXX_STANDARD 20)

set(SOURCE_FILES IMGwcx.cpp resource.h wcxhead.h main_resources.rc)

add_library(IMGwcx64 SHARED ${SOURCE_FILES} )
set_target_properties(IMGwcx64 PROPERTIES OUTPUT_NAME img)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set_target_properties(IMGwcx64 PROPERTIES SUFFIX .wcx64 PREFIX "") 
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set_target_properties(IMGwcx64 PROPERTIES SUFFIX .wcx PREFIX "") 	
endif()
