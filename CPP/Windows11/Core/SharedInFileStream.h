// SharedInFileStream.h

#ifndef ZIP7_INC_SHARED_IN_FILE_STREAM_H
#define ZIP7_INC_SHARED_IN_FILE_STREAM_H

#include "../../Common/MyCom.h"

#include "../../Windows/FileIO.h"

#include "../../7zip/IStream.h"

/*
Input file stream that lets other programs keep the file open for writing,
renaming or deleting (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE),
as File Explorer does. CInFileStream::OpenShared() doesn't share delete
access, so a file held by a program that asked for DELETE access couldn't be
opened, hashed or compressed.
*/

Z7_CLASS_IMP_COM_2(
  CSharedInFileStream
  , IInStream
  , IStreamGetSize
)
  Z7_IFACE_COM7_IMP(ISequentialInStream)

  NWindows::NFile::NIO::CInFile _file;
public:
  bool Open(CFSTR path);
};

namespace NSharedFile {

// Opens a file for reading with the same sharing as CSharedInFileStream.
bool OpenRead(NWindows::NFile::NIO::CInFile &file, CFSTR path);

}

#endif
