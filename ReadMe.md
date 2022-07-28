Introduction
============

This project is intended to create a wcx (archive) plugin for 64-bit Total Commander (TCmd) supporting access to the floppy disk images and FAT images in general.
There are several such good plugins for the 32-bit TCmd, but none (to the best of my knowledge) for the 64-bit TCmd. 
IMG Plugin for Total Commander (TCmd) by IvGzury was used as a starting point because its sources were available.

Supports: FAT12, FAT16, FAT32, VFAT.

Supports DOS 1.xx images without BPB. Support is based on the media descriptor in the FAT table and image size. Added several exceptions 
for the popular quirks of images on the retro sites.

Original [img readme](orig_img_read.txt)

Installation
============

As usual for the TCmd: can be installed manually and archive with pluginst.inf provided, TCmd would propose 
to install the plugin when opening the archive. 

Works in Double Commander for Windows.

Binary releases available [here](https://github.com/indrekis/FDDImage_TCMD_plugin/releases).

Compilation
===========

Code can be compiled using the Visual Studio project or CMakeLists.txt (tested using MSVC and MinGW). Uses C++20, with no external dependencies.

Problems
========
* Read-only
* Support for the exFAT not planned (yet) 
* No support for Unicode, ASCII only
* No support for the 8" images, including 86-DOS images
* Directories data, time, and some attributes are not set properly
* Does not go into the empty dirs
