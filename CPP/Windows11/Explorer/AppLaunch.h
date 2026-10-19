// AppLaunch.h

#ifndef ZIP7_INC_APP_LAUNCH_H
#define ZIP7_INC_APP_LAUNCH_H

#include "../../Common/MyString.h"

// The package family name of the package that contains this DLL
// (works both inside and outside of package context).
bool GetPackageFamilyName(UString &name);

// "<dir of this DLL>\SevenZip.exe"
UString GetAppExePath();

/* Builds the app command line: verb "path1" "path2" ...
   Big selections go through a single-use @listfile in %TEMP% (UTF-16 LE lines),
   which the app deletes after reading. */
HRESULT BuildAppArguments(const char *verb, const UStringVector &paths, UString &args);

// Starts the app with package identity (through its AUMID), or SevenZip.exe next to this DLL.
HRESULT LaunchApp(const UString &args);

#endif
