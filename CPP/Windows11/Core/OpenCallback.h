// OpenCallback.h

#ifndef ZIP7_INC_OPEN_CALLBACK_H
#define ZIP7_INC_OPEN_CALLBACK_H

#include "../../Common/MyCom.h"
#include "../../Common/MyString.h"

#include "../../7zip/Archive/IArchive.h"
#include "../../7zip/IPassword.h"

#include "OperationContext.h"

/*
Open-time callback: progress, multi-volume stream lookup (.7z.001, .part2.rar, .z01...) and
password requests for archives with encrypted headers.
*/
Z7_CLASS_IMP_COM_3(
  COpenCallback
  , IArchiveOpenCallback
  , IArchiveOpenVolumeCallback
  , ICryptoGetTextPassword
)
  COperationContext *_ctx;
  FString _directory; // with the trailing separator
  UString _archiveName;
  UString _currentName;
public:
  bool Password_Defined;
  bool PasswordRequested;
  bool PasswordCancelled;
  // Set by the caller when the engine has no on-disk volumes (nested/split inner streams).
  bool DisableVolumes;
  UString Password;
  FStringVector VolumePaths;

  COpenCallback():
      _ctx(NULL),
      Password_Defined(false),
      PasswordRequested(false),
      PasswordCancelled(false),
      DisableVolumes(false)
      {}

  void Init(const FString &archivePath, COperationContext *ctx, bool password_Defined, const UString &password);
};

#endif
