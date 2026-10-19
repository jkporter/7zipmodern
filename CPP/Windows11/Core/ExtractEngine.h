// ExtractEngine.h

#ifndef ZIP7_INC_EXTRACT_ENGINE_H
#define ZIP7_INC_EXTRACT_ENGINE_H

#include "../../Common/MyVector.h"

#include "ArchiveSession.h"
#include "ExtractOptions.h"
#include "OperationContext.h"

namespace NExtractEngine {

/* Extracts the given items ((indices) == NULL: everything) and fills (result).
   Per-item problems (CRC errors, a wrong password, files that can't be created) don't fail the call:
   they are counted in result.Errors and reported through ctx.AddMessage().
   result.WrongPassword tells that the password was wrong (the session password is then forgotten).
   Returns E_ABORT if cancelled (including a cancelled password or overwrite prompt). */
HRESULT Extract(CArchiveSession &session, const CRecordVector<UInt32> *indices, const CExtractOptions &options,
    COperationContext &ctx, CExtractResult &result, UString &errorMessage);

// Decodes and verifies items without writing anything.
HRESULT Test(CArchiveSession &session, const CRecordVector<UInt32> *indices,
    COperationContext &ctx, CExtractResult &result, UString &errorMessage);

// Expands folder selections into item indices (folders include all descendants); sorted, unique.
void CollectIndices(const CRecordVector<const CArchiveEntry *> &files,
    const CRecordVector<const CArchiveFolder *> &folders, CRecordVector<UInt32> &indices);

}

#endif
