Introduction
============

This project is intended to create a plugin for 64-bit Total Commander (TCmd) supporting access to the floppy disk images and FAT images in general.
There are several such good plugins for the 32-bit TCmd, but none (to the best of my knowledge) for the 64-bit TCmd. 
IMG Plugin for Total Commander (TCmd) by IvGzury was used as a starting point because its sources were available.

Original [img readme](orig_img_read.txt)

Installation
============

As usual for the TCmd. Works in Double Commander for Windows.

Compilation
===========

Code can be compiled using the Visual Studio project or CMakeLists.txt (tested using MSVC and MinGW). Uses C++20, with no external dependencies.

Problems
========
* Read-only
* Only FAT12
* No support for long filenames
* Directories data, time, and some attributes are not set properly
