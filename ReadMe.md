Introduction
============

This project is intended to create a wcx (archive) plugin for 64-bit Total Commander (TCmd) 
supporting access to the floppy disk images and FAT images in general.
There are several such good plugins for the 32-bit TCmd, but none (to the best of my knowledge) for the 64-bit TCmd. 
IMG Plugin for Total Commander (TCmd) by IvGzury was used as a starting point because its sources were available.

Supports: FAT12, FAT16, FAT32, VFAT.

Supports DOS 1.xx images without BPB. Support is based on the media descriptor in the FAT table and image size. Added several exceptions 
for the popular quirks of historical disk images from the retro sites.

Supports MBR-based partitions. Volumes with unknown filesystems are shown as such.

The plugin supports searching for the boot sector in the image. This is intended to help open images containing some metadata at 
the beginning, added by imaging tools. WinImage demonstrates the same behavior. Though the partitioned disks are not yet supported, 
this feature allows the opening of the first FAT partition on partitioned disk images. 
The boot sector for this search is determined by the following pattern of size 512 bytes exactly: 

`0xB8, 0xx, 0x90, 0xx ... 0xx, 0x55, 0xAA`

where 0xx means any byte. The search range currently is equal to 64kb.


Installation
============

As usual for the TCmd plugins:
* Manually:
	1. Unzip the WCX to the directory of your choice (for example, c:\wincmd\plugins\wcx)
	2. In TCmd, choose Configuration - Options
	3. Open the 'Packer' page
	4. Click 'Configure packer extension WCXs'
	5. Type 'img' as the extension
	6. Click 'new type', and select the img.wcx64 (img.wcx for 32-bit TCmd)
	7. Click OK
* Using automated install:
	1. Open the archive with the plugin in TCmd -- it would propose to install the plugin. 

Configuration is stored in the `fatdiskimg.ini` file in the configuration path provided by the TCmd (usually -- the same place, where wincmd.ini is located). If the configuration file is damaged or absent, it is recreated with default values.

Works in Double Commander for Windows.

Binary releases available [here](https://github.com/indrekis/FDDImage_TCMD_plugin/releases).

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

Plugin configuration
====================

Configuration file, named fatdiskimg.ini, is searched at the path, provided by the TCmd (most often -- the path where wincmd.ini is located). If configuration file is absent or incorrect it is created with the default configuration.

Compilation
===========

Code can be compiled using the Visual Studio project or CMakeLists.txt (tested using MSVC and MinGW). Uses C++20, with no obligatory external dependencies. 

As an experimental and optional feature, [FLTK](https://www.fltk.org/) is used to create dialogs and a logging window. 

Problems and limitations
========================
* Read-only
* Does not support exFAT 
* No GPT support 
* No support for Unicode, ASCII only
* No support for the 8" images, including 86-DOS images
* Directories data, time, and some attributes are not set properly
* Not yet tested on large (close to 2Tb) images.

