// MenuSettings.h

#ifndef ZIP7_INC_MENU_SETTINGS_H
#define ZIP7_INC_MENU_SETTINGS_H

#include "../../Common/MyString.h"

/* contextmenu.ini is written by the app (Settings page): one "Key=1" / "Key=0" line
   per menu command, UTF-16 LE with BOM. The app keeps it in its package LocalState
   folder, or in "%LOCALAPPDATA%\7-Zip WinUI" when it runs unpackaged. */

class CMenuSettings
{
  UString _text; // "\n" + file text without CRs, so every key is found as "\nKey="

  bool Load(CFSTR path);
public:
  void Read();
  bool IsOn(const char *key) const; // keys that are not in the file are on
};

#endif
