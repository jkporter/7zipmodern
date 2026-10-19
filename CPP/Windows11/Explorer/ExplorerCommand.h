// ExplorerCommand.h

#ifndef ZIP7_INC_EXPLORER_COMMAND_H
#define ZIP7_INC_EXPLORER_COMMAND_H

#include <ShObjIdl_core.h>

#include "../../Common/MyCom.h"
#include "../../Common/MyString.h"

#include "../../7zip/IDecl.h"
#include "../../7zip/UI/FileManager/MyCom2.h"

/*
Windows 11 File Explorer context menu for 7-Zip (IExplorerCommand), replacing the classic
IContextMenu handler in CPP/7zip/UI/Explorer. It is registered by the MSIX manifest
(desktop4:FileExplorerContextMenus + com:SurrogateServer), so it shows in the modern top-level
menu instead of under "Show more options", and needs no registry writes or admin rights.

The handler only decides which commands to show and launches the app with a verb;
all archive work happens in the app. It never loads 7z.dll into Explorer.
*/

// {AB1904A6-9AFE-48AE-8A8C-B605FD3135F5} : must match Package.appxmanifest
Z7_DEFINE_GUID(CLSID_CSevenZipCommand,
    0xAB1904A6, 0x9AFE, 0x48AE, 0x8A, 0x8C, 0xB6, 0x05, 0xFD, 0x31, 0x35, 0xF5);

// method lists of the SDK interfaces, for Z7_IFACE_COM7_IMP()

#define Z7_IFACEM_IExplorerCommand(x) \
  x(GetTitle(IShellItemArray *items, LPWSTR *name)) \
  x(GetIcon(IShellItemArray *items, LPWSTR *icon)) \
  x(GetToolTip(IShellItemArray *items, LPWSTR *infoTip)) \
  x(GetCanonicalName(GUID *commandName)) \
  x(GetState(IShellItemArray *items, BOOL okToBeSlow, EXPCMDSTATE *state)) \
  x(Invoke(IShellItemArray *items, IBindCtx *bindCtx)) \
  x(GetFlags(EXPCMDFLAGS *flags)) \
  x(EnumSubCommands(IEnumExplorerCommand **enumCommands)) \

#define Z7_IFACEM_IEnumExplorerCommand(x) \
  x(Next(ULONG numCommands, IExplorerCommand **commands, ULONG *numFetched)) \
  x(Skip(ULONG numCommands)) \
  x(Reset()) \
  x(Clone(IEnumExplorerCommand **enumCommands)) \


namespace NSelection
{
  enum EEnum
  {
    kAny,
    kArchivesOnly,
    kSingleArchive
  };
}

struct CCommandInfo
{
  const char *Id;          // key in contextmenu.ini
  const char *Verb;        // verb on the app command line
  const wchar_t *Title;    // "%s" is replaced with the computed name
  NSelection::EEnum Needs;
  bool SeparatorBefore;
};


// the top-level "7-Zip" item with its submenu

class CRootCommand Z7_final:
  public IExplorerCommand,
  public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_1_MT(IExplorerCommand)
  Z7_IFACE_COM7_IMP(IExplorerCommand)
public:
  CRootCommand();
  ~CRootCommand();
};


class CSubCommand Z7_final:
  public IExplorerCommand,
  public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_1_MT(IExplorerCommand)
  Z7_IFACE_COM7_IMP(IExplorerCommand)

  const CCommandInfo &_info;
public:
  CSubCommand(const CCommandInfo &info);
  ~CSubCommand();
  bool Applies(const UStringVector &paths) const;
};


class CSeparatorCommand Z7_final:
  public IExplorerCommand,
  public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_1_MT(IExplorerCommand)
  Z7_IFACE_COM7_IMP(IExplorerCommand)

  bool _onlyWithArchives;
public:
  CSeparatorCommand(bool onlyWithArchives);
  ~CSeparatorCommand();
};


class CCommandEnumerator Z7_final:
  public IEnumExplorerCommand,
  public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_1_MT(IEnumExplorerCommand)
  Z7_IFACE_COM7_IMP(IEnumExplorerCommand)

  unsigned _index;
public:
  CObjectVector< CMyComPtr<IExplorerCommand> > Commands;

  CCommandEnumerator();
  ~CCommandEnumerator();
};

#endif
