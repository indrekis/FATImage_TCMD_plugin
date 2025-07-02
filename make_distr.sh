#!/bin/bash

# Currently -- Windows only.
# TODO: add creation of the plugin archives

# x86-64 + FLTK + debug

if [ -z "${VCPKG_BASE+x}" ]; then
    VCPKG_BASE="c:/WorkComp/vcpkg2024/vcpkg"
fi

VCPKG_PATH=${VCPKG_BASE}/scripts/buildsystems/vcpkg.cmake

echo "============= x86_64_dbg_FLTK =============="
# x86-64 + debug + FLTK
mkdir -p distr/x86_64_dbg_FLTK
rm -rf ./tbuild
mkdir ./tbuild
cd tbuild
cmake -A x64 .. -G"Visual Studio 17" -DFLTK_ENABLED_EXPERIMENTAL=1 -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH}
cmake --build . --config Debug 
cp Debug/fatimg.wcx64 ../distr/x86_64_dbg_FLTK/
cd ..

echo "============= x86_64_rel_FLTK =============="
# x86-64 + release + FLTK
mkdir -p distr/x86_64_rel_FLTK
rm -rf ./tbuild
mkdir ./tbuild
cd tbuild
cmake -A x64 .. -G"Visual Studio 17" -DFLTK_ENABLED_EXPERIMENTAL=1 -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH}
cmake --build . --config Release 
cp Release/fatimg.wcx64 ../distr/x86_64_rel_FLTK/
cd ..

echo "============= x86_64_dbg_noFLTK =============="
# x86-64 + debug 
mkdir -p distr/x86_64_dbg_noFLTK
rm -rf ./tbuild
mkdir ./tbuild
cd tbuild
cmake -A x64 .. -G"Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH} -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH}
cmake --build . --config Debug
cp Debug/fatimg.wcx64 ../distr/x86_64_dbg_noFLTK/
cd ..


echo "============= x86_64_rel_noFLTK =============="
# x86-64 + release
mkdir -p distr/x86_64_rel_noFLTK
rm -rf ./tbuild
mkdir ./tbuild
cd tbuild
cmake -A x64 .. -G"Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH} -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH}
cmake --build . --config Release
cp Release/fatimg.wcx64 ../distr/x86_64_rel_noFLTK/
cd ..

echo "============= x86_32_dbg_FLTK =============="
# x86-32 + FLTK + debug
mkdir -p distr/x86_32_dbg_FLTK
rm -rf ./tbuild
mkdir ./tbuild
cd tbuild
cmake -A Win32 .. -G"Visual Studio 17" -DFLTK_ENABLED_EXPERIMENTAL=1 -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH}
cmake --build . --config Debug 
cp Debug/fatimg.wcx ../distr/x86_32_dbg_FLTK
cd ..

echo "============= x86_32_rel_FLTK =============="
# x86-32 + FLTK + Release
mkdir -p distr/x86_32_rel_FLTK
rm -rf ./tbuild
mkdir ./tbuild
cd tbuild
cmake -A Win32 .. -G"Visual Studio 17" -DFLTK_ENABLED_EXPERIMENTAL=1 -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH}
cmake --build . --config Release
cp Release/fatimg.wcx ../distr/x86_32_rel_FLTK
cd ..

echo "============= x86_32_dbg_noFLTK =============="
# x86-32 + debug
mkdir -p distr/x86_32_dbg_noFLTK
rm -rf ./tbuild
mkdir ./tbuild
cd tbuild
cmake -A Win32 .. -G"Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH}  -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH}
cmake --build . --config Debug
cp Debug/fatimg.wcx ../distr/x86_32_dbg_noFLTK
cd ..

echo "============= x86_32_rel_noFLTK =============="
# x86-32 + release
mkdir -p distr/x86_32_rel_noFLTK
rm -rf ./tbuild
mkdir ./tbuild
cd tbuild
cmake -A Win32 .. -G"Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH}  -DCMAKE_TOOLCHAIN_FILE=${VCPKG_PATH}
cmake --build . --config Release
cp Release/fatimg.wcx ../distr/x86_32_rel_noFLTK
cd ..


exit 0 
