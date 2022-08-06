// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* Floppy disk images unpack plugin for the Total Commander.
* Copyright (c) 2002, IvGzury ( ivgzury@hotmail.com )
* Copyright (c) 2022, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* Oleg Farenyuk's code is released under the MIT License.
*
* Original IvGzury copyright message:
* This program is absolutely free software.
* If you have any remarks or problems, please don't
* hesitate to send me an email.
*/
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>
#include <cstdint>

#include "wcxhead.h"
#include "sysui_winapi.h"

int winAPI_msgbox_on_bad_BPB(void*) {
	//! Is it correct to create own dialogs in plugin?
	int msgboxID = MessageBoxEx(
		NULL,
		TEXT("Wrong BPB signature\nContinue?"),
		TEXT("BPB Signature error"),
		MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1,
		0
	);
	if (msgboxID == IDCANCEL) {
		return E_BAD_ARCHIVE;
	}
	else {
		return 0;
	}
}
