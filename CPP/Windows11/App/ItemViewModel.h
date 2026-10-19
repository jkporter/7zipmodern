// ItemViewModel.h

#ifndef ZIP7_INC_ITEM_VIEW_MODEL_H
#define ZIP7_INC_ITEM_VIEW_MODEL_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../Bindable.h"

#include "../Core/ArchiveSession.h"

/*
An open CArchiveSession shared by the view model and the rows that point into
its Entries / Root (C# relied on the GC for that). The session is closed when
the last reference goes away. Reference counting is thread-safe, but keep
references on the UI thread and give worker code raw pointers.
*/

class CArchiveSessionHolder: public winrt::implements<CArchiveSessionHolder, winrt::Windows::Foundation::IInspectable>
{
public:
  CArchiveSession Session;
};


/*
A row in the archive browser: a file entry or a (possibly synthesised) folder.
Archives can hold tens of thousands of items, so a row is a small object: it
implements ICustomPropertyProvider itself with one property table shared by all
rows (CBindableT keeps a table per object), the texts are formatted when a row
is realized, and the type name is looked up on first use.
Bindable ({Binding Name}): Name, Path, Location, IsFolder, IsEncrypted, Icon,
TypeName, SizeText, PackedText, ModifiedText, RatioText, CrcText, Method,
AutomationName. UI thread only.
*/

// Marks CItemViewModel objects, so FromObject() can tell them from other data contexts (no RTTI).
DECLARE_INTERFACE_IID_(IArchiveItemMarker, IUnknown, "6a0c1e52-3f7b-4d8e-9a21-7c5b0f3d8e41")
{
};

class CItemViewModel: public winrt::implements<CItemViewModel,
    winrt::Microsoft::UI::Xaml::Data::ICustomPropertyProvider, IArchiveItemMarker>
{
  winrt::com_ptr<CArchiveSessionHolder> _session; // keeps Entry / Folder valid
  UString _typeName;
  bool _typeName_Defined;
public:
  const CArchiveEntry *Entry;   // file entry, or the explicit entry of a folder; NULL for synthesised folders
  const CArchiveFolder *Folder; // NULL for files
  UString Name;
  UString Path;     // path inside the archive
  UString Location; // folder part of Path, shown in search results
  bool IsFolder;
  bool IsEncrypted;
  bool PackedSize_Defined;
  bool Modified_Defined;
  UInt32 ChildCount;
  UInt64 Size;
  UInt64 PackedSize;
  FILETIME Modified; // UTC

  CItemViewModel();

  // the session this row points into (rows of an older session must not be used for engine calls)
  CArchiveSessionHolder *GetSessionHolder() const { return _session.get(); }

  static winrt::com_ptr<CItemViewModel> FromFolder(CArchiveSessionHolder *session,
      const CArchiveFolder &folder, bool showLocation = false);
  static winrt::com_ptr<CItemViewModel> FromEntry(CArchiveSessionHolder *session,
      const CArchiveEntry &entry, bool showLocation = false);

  // from an element of ItemsControl.Items / SelectedItems (NULL if it is something else)
  static CItemViewModel *FromObject(const winrt::Windows::Foundation::IInspectable &obj);

  const UString &GetTypeName();
  winrt::Microsoft::UI::Xaml::Media::ImageSource GetIcon() const;
  UString GetSizeText() const;
  UString GetPackedText() const;
  UString GetModifiedText() const;
  UString GetRatioText() const;
  UString GetCrcText() const;
  UString GetAutomationName();

  winrt::Windows::Foundation::IInspectable GetPropertyValue(unsigned id);

  // ICustomPropertyProvider
  winrt::Microsoft::UI::Xaml::Data::ICustomProperty GetCustomProperty(const winrt::hstring &name);
  winrt::Microsoft::UI::Xaml::Data::ICustomProperty GetIndexedProperty(
      const winrt::hstring &, const winrt::Windows::UI::Xaml::Interop::TypeName &) { return nullptr; }
  winrt::hstring GetStringRepresentation();
  winrt::Windows::UI::Xaml::Interop::TypeName Type();
};

typedef CObjectVector<winrt::com_ptr<CItemViewModel> > CItemVector;


// A "name: value" row of the details pane (C# PropertyRow). Bindable: Name, Value.

class CPropertyRow: public NWindows11::CBindableT<CPropertyRow>
{
public:
  UString Name;
  UString Value;
  CPropertyRow(const UString &name, const UString &value);
};

// Short date + short time in the user's format (.NET "g"), UTC input.
UString FormatShortDateTime(const FILETIME &utc);

#endif
