// BenchmarkRunner.cpp

#include "StdAfx.h"

#include "BenchmarkRunner.h"

#include "../../Common/ComTry.h"
#include "../../Common/MyCom.h"

#include "../../Windows/PropVariant.h"

#include "../../7zip/Common/StreamObjects.h"

#include "ArchiveFormat.h"
#include "ArchiveWriter.h"
#include "SevenZipEngine.h"
#include "UpdateCallback.h"

using namespace NWindows;

// Seekable in-memory IOutStream: the 7z writer seeks back to patch the start header,
// and 7-Zip has no memory stream with IOutStream.
Z7_CLASS_IMP_COM_1(
  CMemOutStream
  , IOutStream
)
  Z7_IFACE_COM7_IMP(ISequentialOutStream)

  CByteDynBuffer _buf;
  size_t _size;
  size_t _pos;

  bool Reserve(size_t size)
  {
    const size_t old = _buf.GetCapacity();
    if (!_buf.EnsureCapacity(size))
      return false;
    if (_buf.GetCapacity() > old)
      memset((Byte *)_buf + old, 0, _buf.GetCapacity() - old);
    return true;
  }
public:
  CMemOutStream(): _size(0), _pos(0) {}
  const Byte *GetData() const { return _buf; }
  size_t GetSize() const { return _size; }
};

Z7_COM7F_IMF(CMemOutStream::Write(const void *data, UInt32 size, UInt32 *processedSize))
{
  if (processedSize)
    *processedSize = 0;
  if (size == 0)
    return S_OK;
  if (!Reserve(_pos + size))
    return E_OUTOFMEMORY;
  memcpy((Byte *)_buf + _pos, data, size);
  _pos += size;
  if (_size < _pos)
    _size = _pos;
  if (processedSize)
    *processedSize = size;
  return S_OK;
}

Z7_COM7F_IMF(CMemOutStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition))
{
  Int64 base;
  switch (seekOrigin)
  {
    case STREAM_SEEK_SET: base = 0; break;
    case STREAM_SEEK_CUR: base = (Int64)_pos; break;
    case STREAM_SEEK_END: base = (Int64)_size; break;
    default: return STG_E_INVALIDFUNCTION;
  }
  const Int64 pos = base + offset;
  if (pos < 0)
    return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;
  _pos = (size_t)pos;
  if (newPosition)
    *newPosition = (UInt64)pos;
  return S_OK;
}

Z7_COM7F_IMF(CMemOutStream::SetSize(UInt64 newSize))
{
  if (!Reserve((size_t)newSize))
    return E_OUTOFMEMORY;
  if (newSize < _size)
    memset((Byte *)_buf + (size_t)newSize, 0, _size - (size_t)newSize);
  _size = (size_t)newSize;
  return S_OK;
}


// Write-only sink used when benchmarking (data is discarded and counted).
Z7_CLASS_IMP_COM_1(
  CCountingSink
  , ISequentialOutStream
)
public:
  UInt64 Bytes;
  CCountingSink(): Bytes(0) {}
};

Z7_COM7F_IMF(CCountingSink::Write(const void * /* data */, UInt32 size, UInt32 *processedSize))
{
  Bytes += size;
  if (processedSize)
    *processedSize = size;
  return S_OK;
}


Z7_CLASS_IMP_COM_1(
  CBenchExtractCallback
  , IArchiveExtractCallback
)
  Z7_IFACE_COM7_IMP(IProgress)
  COperationContext *_ctx;
public:
  CCountingSink *SinkSpec;
  CMyComPtr<ISequentialOutStream> Sink;
  bool Failed;

  CBenchExtractCallback(COperationContext *ctx): _ctx(ctx), Failed(false)
  {
    SinkSpec = new CCountingSink;
    Sink = SinkSpec;
  }
};

Z7_COM7F_IMF(CBenchExtractCallback::SetTotal(UInt64 /* total */))
{
  return _ctx->Checkpoint();
}

Z7_COM7F_IMF(CBenchExtractCallback::SetCompleted(const UInt64 * /* completeValue */))
{
  return _ctx->Checkpoint();
}

Z7_COM7F_IMF(CBenchExtractCallback::GetStream(UInt32 /* index */, ISequentialOutStream **outStream, Int32 askExtractMode))
{
  *outStream = NULL;
  if (askExtractMode == NArchive::NExtract::NAskMode::kExtract)
  {
    *outStream = Sink;
    Sink->AddRef();
  }
  return S_OK;
}

Z7_COM7F_IMF(CBenchExtractCallback::PrepareOperation(Int32 /* askExtractMode */))
{
  return S_OK;
}

Z7_COM7F_IMF(CBenchExtractCallback::SetOperationResult(Int32 opRes))
{
  if (opRes != NArchive::NExtract::NOperationResult::kOK)
    Failed = true;
  return S_OK;
}


namespace NBenchmarkRunner {

// Small deterministic generator (xorshift64*); the data only needs to be repeatable.
class CRandom
{
  UInt64 _s;
public:
  CRandom(UInt32 seed): _s(((UInt64)seed << 32) ^ 0x9E3779B97F4A7C15) {}
  UInt32 Next32()
  {
    _s ^= _s >> 12;
    _s ^= _s << 25;
    _s ^= _s >> 27;
    return (UInt32)((_s * 0x2545F4914F6CDD1D) >> 32);
  }
  // [0, n)
  UInt32 Next(UInt32 n) { return (UInt32)(((UInt64)Next32() * n) >> 32); }
  // [a, b)
  UInt32 Next(UInt32 a, UInt32 b) { return a + Next(b - a); }
};

void GenerateData(size_t size, CByteBuffer &data, UInt32 seed)
{
  data.Alloc(size);
  Byte *p = data;
  CRandom rng(seed);
  size_t pos = 0;
  while (pos < size)
  {
    if (pos > 4096 && rng.Next(4) != 0)
    {
      UInt32 maxDist = (UInt32)1 << rng.Next(4, 22);
      if (maxDist > pos)
        maxDist = (UInt32)pos;
      const size_t dist = rng.Next(1, maxDist);
      size_t len = rng.Next(3, 96);
      if (len > size - pos)
        len = size - pos;
      for (size_t i = 0; i < len; i++, pos++)
        p[pos] = p[pos - dist];
    }
    else
    {
      size_t len = rng.Next(1, 32);
      if (len > size - pos)
        len = size - pos;
      for (size_t i = 0; i < len; i++)
      {
        const UInt32 base = rng.Next(64);
        const UInt32 add = (rng.Next(8) == 0) ? rng.Next(192) : 32;
        p[pos++] = (Byte)(base + add);
      }
    }
  }
}

static UInt64 GetTicks()
{
  LARGE_INTEGER v;
  ::QueryPerformanceCounter(&v);
  return (UInt64)v.QuadPart;
}

static double TicksToSeconds(UInt64 ticks)
{
  LARGE_INTEGER f;
  ::QueryPerformanceFrequency(&f);
  return (double)ticks / (double)f.QuadPart;
}

HRESULT RunPass(UInt32 pass, const CByteBuffer &data, UInt64 dictionary, UInt32 threads,
    COperationContext &ctx, CBenchmarkPass &result, UString &errorMessage)
{
  errorMessage.Empty();
  const CArchiveFormat *format = NFormatRegistry::Find(UString("7z"));
  if (!format)
  {
    errorMessage = "The engine has no '7z' handler.";
    return E_NOTIMPL;
  }

  // Compress
  CMemOutStream *outSpec = new CMemOutStream;
  CMyComPtr<IOutStream> outStream = outSpec;
  {
    CMyComPtr<IOutArchive> outArchive;
    HRESULT res = NSevenZipEngine::CreateOutArchive(format->ClassId, outArchive);
    if (res == S_OK)
    {
      CEnginePropVector props;
      {
        CEngineProp &p = props.AddNew();
        p.Name = "x";
        p.Value = (UInt32)5;
      }
      {
        CEngineProp &p = props.AddNew();
        p.Name = "0";
        p.Value = "LZMA2";
      }
      {
        CEngineProp &p = props.AddNew();
        p.Name = "0d";
        UString v;
        v.Add_UInt64(dictionary);
        v += L'b';
        p.Value = v;
      }
      {
        CEngineProp &p = props.AddNew();
        p.Name = "mt";
        p.Value = (UInt32)threads;
      }
      res = NArchiveWriter::ApplyProperties(outArchive, props);
    }
    if (res != S_OK)
    {
      FormatHResultMessage(res, UString("Benchmark (compress)"), errorMessage);
      return res;
    }

    CObjectVector<CUpdateItem> items;
    CUpdateItem &item = items.AddNew();
    item.NewData = true;
    item.NewProps = true;
    item.ArchivePath = "bench.bin";
    item.Size = data.Size();
    item.Attributes = FILE_ATTRIBUTE_ARCHIVE;
    ::GetSystemTimeAsFileTime(&item.Modified);
    item.Modified_Defined = true;
    item.SourceData = data;

    CUpdateCallback *callbackSpec = new CUpdateCallback;
    CMyComPtr<IArchiveUpdateCallback> callback = callbackSpec;
    callbackSpec->Init(&items, &ctx, UString(), NULL, false, false);
    const UInt64 start = GetTicks();
    res = outArchive->UpdateItems(outStream, 1, callback);
    const double seconds = TicksToSeconds(GetTicks() - start);
    if (res != S_OK)
    {
      FormatHResultMessage(res, UString("Benchmark (compress)"), errorMessage);
      return res;
    }
    result.CompressMBps = seconds <= 0 ? 0 : (double)data.Size() / (1024.0 * 1024.0) / seconds;
  }

  // Decompress
  {
    CBufInStream *inSpec = new CBufInStream;
    CMyComPtr<IInStream> inStream = inSpec;
    inSpec->Init(outSpec->GetData(), outSpec->GetSize(), outStream);
    CMyComPtr<IInArchive> inArchive;
    HRESULT res = NSevenZipEngine::CreateInArchive(format->ClassId, inArchive);
    if (res == S_OK)
    {
      const UInt64 maxCheck = 0;
      res = inArchive->Open(inStream, &maxCheck, NULL);
    }
    if (res != S_OK)
    {
      FormatHResultMessage(res, UString("Benchmark (open)"), errorMessage);
      return res;
    }
    CBenchExtractCallback *sinkSpec = new CBenchExtractCallback(&ctx);
    CMyComPtr<IArchiveExtractCallback> sink = sinkSpec;
    const UInt64 start = GetTicks();
    res = inArchive->Extract(NULL, (UInt32)(Int32)-1, 0, sink);
    const double seconds = TicksToSeconds(GetTicks() - start);
    inArchive->Close();
    if (res != S_OK)
    {
      FormatHResultMessage(res, UString("Benchmark (decompress)"), errorMessage);
      return res;
    }
    if (sinkSpec->Failed || sinkSpec->SinkSpec->Bytes != data.Size())
    {
      errorMessage = "Benchmark verification failed: decompressed data does not match. Your hardware may be unstable.";
      return E_FAIL;
    }
    result.DecompressMBps = seconds <= 0 ? 0 : (double)data.Size() / (1024.0 * 1024.0) / seconds;
  }

  result.Pass = pass;
  result.Ratio = data.Size() == 0 ? 0 : (double)outSpec->GetSize() / (double)data.Size();
  result.Threads = threads;
  result.Dictionary = dictionary;
  return S_OK;
}

}
