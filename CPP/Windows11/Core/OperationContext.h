// OperationContext.h

#ifndef ZIP7_INC_OPERATION_CONTEXT_H
#define ZIP7_INC_OPERATION_CONTEXT_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../../Windows/Synchronization.h"

#include "../../7zip/IDecl.h"

/*
Engine operations (open, extract, test, compress, update, hash, benchmark) are
synchronous functions. The caller runs them on a worker thread and passes a
COperationContext that carries cancellation, the pause gate, progress counters,
the progress sink and the interactive prompts.

Errors: functions return HRESULT and fill (UString &errorMessage).
  S_OK                      - success
  E_ABORT                   - cancelled by the user (Cancel(), a cancelled prompt)
  k_HRESULT_WrongPassword   - the archive needs a password and the given one is wrong
  k_HRESULT_IsNotArc        - the file can't be opened as an archive
  other                     - failure; errorMessage holds the text for the user
*/

const HRESULT k_HRESULT_WrongPassword = (HRESULT)0x80047A01;
const HRESULT k_HRESULT_IsNotArc      = (HRESULT)0x80047A02;

namespace NOperationKind
{
  enum EEnum
  {
    kOpen,
    kExtract,
    kTest,
    kCompress,
    kUpdate,
    kHash,
    kBenchmark
  };
}

namespace NMessageSeverity
{
  enum EEnum
  {
    kInfo,
    kWarning,
    kError
  };
}

struct COperationMessage
{
  NMessageSeverity::EEnum Severity;
  UString Text;
  UString ItemPath; // empty if the message is not about one item
};

// Snapshot of operation progress. Byte counts refer to uncompressed (input) data.
struct COperationProgress
{
  UInt64 TotalBytes;
  UInt64 CompletedBytes;
  UInt64 TotalFiles;
  UInt64 CompletedFiles;
  UInt64 OutputBytes;
  UString CurrentItem;
  UInt64 ElapsedMs; // paused time is excluded

  COperationProgress():
      TotalBytes(0), CompletedBytes(0), TotalFiles(0), CompletedFiles(0),
      OutputBytes(0), ElapsedMs(0) {}

  double GetFraction() const;       // 0 .. 1
  double GetBytesPerSecond() const; // 0 during the first 250 ms
  bool GetRemainingSeconds(UInt64 &seconds) const;
  bool GetRatio(double &ratio) const; // OutputBytes / CompletedBytes
};

namespace NOverwriteChoice
{
  enum EEnum
  {
    kReplace,
    kReplaceAll,
    kSkip,
    kSkipAll,
    kKeepBoth,
    kKeepBothAll,
    kCancel
  };
}

struct COverwriteRequest
{
  FString TargetPath;
  UInt64 ExistingSize;
  bool ExistingModified_Defined;
  FILETIME ExistingModified;
  UInt64 NewSize;
  bool NewModified_Defined;
  FILETIME NewModified;
};

Z7_PURE_INTERFACES_BEGIN

// Called on the worker thread (throttled by COperationContext::ReportIntervalMs).
#define Z7_IFACEN_IOperationProgress(x) \
  virtual void OnProgress(const COperationProgress &progress) x \

Z7_IFACE_DECL_PURE(IOperationProgress)

/* Interactive questions the engine may need answered mid-operation.
   Calls arrive on the worker thread and must block until the user answers.
   RequestPassword: return S_OK and fill (password), or E_ABORT if the user cancels.
   ResolveOverwrite: fill (choice); NOverwriteChoice::kCancel cancels the operation. */
#define Z7_IFACEN_IOperationPrompts(x) \
  virtual HRESULT RequestPassword(const UString &archiveName, bool retry, UString &password) x \
  virtual HRESULT ResolveOverwrite(const COverwriteRequest &request, NOverwriteChoice::EEnum &choice) x \

Z7_IFACE_DECL_PURE(IOperationPrompts)

Z7_PURE_INTERFACES_END


// Cancellation, pause, progress and prompt plumbing shared by all engine operations.
class COperationContext
{
  NWindows::NSynchronization::CManualResetEvent _runGate; // signaled while running
  NWindows::NSynchronization::CManualResetEvent _cancelEvent;
  mutable NWindows::NSynchronization::CCriticalSection _cs;
  CObjectVector<COperationMessage> _messages;
  UString _currentItem;
  LONG _cancelled; // accessed with Interlocked functions
  LONG _paused;
  UInt64 _startTick;
  UInt64 _pausedTotal;
  UInt64 _pauseStarted;
  UInt64 _lastReportTick;

  Z7_CLASS_NO_COPY(COperationContext)
public:
  IOperationProgress *Progress;
  IOperationPrompts *Prompts;

  // Counters are written by the worker thread.
  UInt64 TotalBytes;
  UInt64 CompletedBytes;
  UInt64 TotalFiles;
  UInt64 CompletedFiles;
  UInt64 OutputBytes;

  // Progress throttle interval; the UI only needs ~10 updates per second.
  UInt32 ReportIntervalMs;

  COperationContext(IOperationProgress *progress = NULL, IOperationPrompts *prompts = NULL);

  void SetCurrentItem(const UString &item);
  UString GetCurrentItem() const;

  // Any thread:
  void Cancel();
  bool IsCancelled() const;
  void Pause();
  void Resume();
  bool IsPaused() const;

  void AddMessage(NMessageSeverity::EEnum severity, const UString &text, const UString &item = UString());
  void GetMessages(CObjectVector<COperationMessage> &messages) const;
  bool HasErrors() const;

  UInt64 GetElapsedMs() const;

  // Called from engine callbacks: blocks while paused, returns E_ABORT when cancelled.
  HRESULT Checkpoint();
  void Report(bool force = false);
  void GetProgress(COperationProgress &progress) const;
};

/* Builds the message for a failed engine call: "context: detail".
   E_ABORT gives "The operation was cancelled." */
void FormatHResultMessage(HRESULT hr, const UString &context, UString &message);

#endif
