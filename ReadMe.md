Introduction
============

This project is intended to create a plugin supporting access to the floppy disk images for the modern TCmd.
IMG Plugin for Total Commander (TCmd) by IvGzury is a good plugin for such tasks. But it was never ported to the 64-bit TCmd. 
So II started porting img version 0.9 for which sources were available. 

Original [img readme](orig_img_read.txt)

Installation
============

As for now repository contains compiled binaries of the plugins too (img.wcx and img.wcx64), though, the 32-bit version is 
not thoroughly tested. Installation-ready archives will be provided in the future. 

Problems
========
* Read-only
* Only FAT12
* Only 1 sector per cluster
* No support for long filenames
* Problems with directories data, time and attributes
