// SharedInFileStream.cpp

#include "StdAfx.h"

#include "SharedInFileStream.h"

using namespace NWindows;
using namespace NFile;

static inline HRESULT ConvertBoolToHRESULT(bool result)
{
  return result ? S_OK : GetLastError_noZero_HRESULT();
}

namespace NSharedFile {

bool OpenRead(NIO::CInFile &file, CFSTR path)
{
  return file.Open(path, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
}

}

bool CSharedInFileStream::Open(CFSTR path)
{
  return NSharedFile::OpenRead(_file, path);
}

Z7_COM7F_IMF(CSharedInFileStream::Read(void *data, UInt32 size, UInt32 *processedSize))
{
  UInt32 realProcessedSize = 0;
  const bool result = _file.ReadPart(data, size, realProcessedSize);
  if (processedSize)
    *processedSize = realProcessedSize;
  return ConvertBoolToHRESULT(result);
}

Z7_COM7F_IMF(CSharedInFileStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition))
{
  if (seekOrigin >= 3)
    return STG_E_INVALIDFUNCTION;
  UInt64 realNewPosition = 0;
  const bool result = _file.Seek(offset, seekOrigin, realNewPosition);
  if (newPosition)
    *newPosition = realNewPosition;
  return ConvertBoolToHRESULT(result);
}

Z7_COM7F_IMF(CSharedInFileStream::GetSize(UInt64 *size))
{
  return ConvertBoolToHRESULT(_file.GetLength(*size));
}
