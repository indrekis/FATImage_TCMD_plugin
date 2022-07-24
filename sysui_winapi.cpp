#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>
#include <cstdint>

#include "wcxhead.h"
#include "sysui_winapi.h"

int winAPI_msgbox_on_bad_BPB(void*, int openmode) {
	if (openmode == PK_OM_LIST) {
		//! Is it correct to create own dialogs in plugin?
		int msgboxID = MessageBoxEx(
			NULL,
			TEXT("Wrong BPB signature\nContinue?"),
			TEXT("BPB Signature error"),
			MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1,
			0
		);
		if (msgboxID == IDCANCEL) {
			return E_UNKNOWN_FORMAT;
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
}
