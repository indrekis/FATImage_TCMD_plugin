Introduction
============

This student project is an attempt to implement write-mode support for the FATImage_TCMD_plugin, a Total Commander plugin designed to work with FAT-formatted disk images. 
While the original plugin provided read-only access, this extended version aims to enable the creation, modification, and addition of files and directories within FAT images.
The implementation is based on the FATFs library, allowing for structured and low-level manipulation of FAT file systems.

The original plugin is available here: https://github.com/indrekis/FATImage_TCMD_plugin
It provides functionality for browsing and extracting files from FAT12/16/32 disk images within Total Commander.

FATImage is a wcx (archive) plugin for 64-bit and 32-bit Total Commander (TCmd) 
supporting access to the floppy disk images and FAT images in general, including partitioned.
> There are several such good plugins for the 32-bit TCmd, but none (to the best of my knowledge) for the 64-bit TCmd. 
> > IMG Plugin for Total Commander (TCmd) by IvGzury was used as a starting point because its sources were available.

Supports: FAT12, FAT16, FAT32, VFAT.


                
