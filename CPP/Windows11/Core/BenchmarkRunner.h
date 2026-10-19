// BenchmarkRunner.h

#ifndef ZIP7_INC_BENCHMARK_RUNNER_H
#define ZIP7_INC_BENCHMARK_RUNNER_H

#include "../../Common/MyBuffer.h"
#include "../../Common/MyString.h"

#include "OperationContext.h"

struct CBenchmarkPass
{
  UInt32 Pass;
  double CompressMBps;
  double DecompressMBps;
  double Ratio; // compressed / original
  UInt32 Threads;
  UInt64 Dictionary;

  CBenchmarkPass(): Pass(0), CompressMBps(0), DecompressMBps(0), Ratio(0), Threads(0), Dictionary(0) {}
};

/*
LZMA2 round-trip benchmark through the real engine (the classic dialog's numbers, simplified):
compresses generated, moderately compressible data in memory and decompresses it again.
*/
namespace NBenchmarkRunner {

// Mix of literal noise and back-references, ~3:1 under LZMA like typical binaries/text.
void GenerateData(size_t size, CByteBuffer &data, UInt32 seed = 0x7A1);

/* Returns S_OK, E_ABORT, or an error. A decompression mismatch returns E_FAIL with
   "Benchmark verification failed..." in (errorMessage). */
HRESULT RunPass(UInt32 pass, const CByteBuffer &data, UInt64 dictionary, UInt32 threads,
    COperationContext &ctx, CBenchmarkPass &result, UString &errorMessage);

}

#endif
