// OperationWindow.cpp

#include "StdAfx.h"

#include "OperationWindow.h"

#include "../../Windows/DLL.h"
#include "../../Windows/FileFind.h"
#include "../../Windows/ProcessUtils.h"

#include "../Dispatcher.h"
#include "../HString.h"
#include "../Xaml.h"

#include "../Core/ArchiveWriter.h"
#include "../Core/CompressionSettings.h"
#include "../Core/ExtractEngine.h"
#include "../Core/PropertyFormatter.h"

#include "App.h"
#include "PlatformServices.h"
#include "ShellServices.h"
#include "ThemeHelper.h"
#include "resource.h"

using namespace winrt;
using namespace winrt::Microsoft::UI;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;
using namespace NWindows;
using namespace NFile;
using namespace NWindows11;

static const wchar_t * const kCancelledText = L"Cancelled.";

static UString GetFileName(const UString &path)
{
  return UString(path.Ptr((unsigned)(path.ReverseFind_PathSepar() + 1)));
}

static UString GetDirectory(const UString &path)
{
  UString s (path);
  const int slash = s.ReverseFind_PathSepar();
  if (slash < 0)
    return UString();
  // keep "C:\" for files in a root folder
  s.DeleteFrom((unsigned)(slash == 2 && s[1] == ':' ? 3 : slash));
  return s;
}

static UString CombinePath(const UString &dir, const UString &name)
{
  UString s (dir);
  if (!s.IsEmpty() && !IS_PATH_SEPAR(s.Back()))
    s.Add_PathSepar();
  s += name;
  return s;
}

// the engine may already have reported the cancel
static bool HasCancelledMessage(const CObjectVector<COperationMessage> &messages)
{
  FOR_VECTOR (i, messages)
    if (messages[i].Text == kCancelledText)
      return true;
  return false;
}

static UString FormatPercent(double ratio)
{
  UString s;
  s.Add_UInt64((UInt64)(ratio * 100 + 0.5));
  s += "%";
  return s;
}


COperationWindow::COperationWindow(const CLaunchRequest &request):
    _request(request),
    _hwnd(NULL),
    _optionsMode(false),
    _started(false),
    _finished(false),
    _closed(false)
{
  _root = NXaml::Load<Grid>(IDX_OPERATION_WINDOW);
  _titleIcon = NXaml::FindName<Image>(_root, L"TitleIcon");
  _titleBarText = NXaml::FindName<TextBlock>(_root, L"TitleBarText");
  _titleText = NXaml::FindName<TextBlock>(_root, L"TitleText");
  _optionsHost = NXaml::FindName<ContentPresenter>(_root, L"OptionsHost");
  _progressPanel = NXaml::FindName<StackPanel>(_root, L"ProgressPanel");
  _detailText = NXaml::FindName<TextBlock>(_root, L"DetailText");
  _progress = NXaml::FindName<ProgressBar>(_root, L"Progress");
  _statsText = NXaml::FindName<TextBlock>(_root, L"StatsText");
  _resultBar = NXaml::FindName<InfoBar>(_root, L"ResultBar");
  _messageList = NXaml::FindName<ListView>(_root, L"MessageList");
  _efficiencyToggle = NXaml::FindName<Primitives::ToggleButton>(_root, L"EfficiencyToggle");
  _primaryButton = NXaml::FindName<Button>(_root, L"PrimaryButton");
  _secondaryButton = NXaml::FindName<Button>(_root, L"SecondaryButton");

  _window.Content(_root);
  _window.as<IWindowNative>()->get_WindowHandle(&_hwnd);
  _window.ExtendsContentIntoTitleBar(true);
  _window.SetTitleBar(NXaml::FindName<UIElement>(_root, L"TitleArea"));
  FString icon = NDLL::GetModuleDirPrefix();
  icon += "Assets\\7zip.ico";
  _window.AppWindow().SetIcon(ToHString(fs2us(icon)));
  _window.AppWindow().Title(L"7-Zip");
  const Windowing::OverlappedPresenter presenter = _window.AppWindow().Presenter().try_as<Windowing::OverlappedPresenter>();
  if (presenter)
  {
    presenter.IsMaximizable(false);
    presenter.IsResizable(false);
  }
  ResizeClient(520, 340);

  _dialogs.Init(_root, _hwnd);
  _operations.Init(_hwnd);
  _root.ActualThemeChanged([this](auto &&, auto &&) { NThemeHelper::UpdateCaptionButtons(_window); });
  // Title bar content fades when the window is inactive, matching the system caption buttons.
  _window.Activated([this](const IInspectable &, const WindowActivatedEventArgs &e)
  {
    const double opacity = (e.WindowActivationState() == WindowActivationState::Deactivated) ? 0.5 : 1;
    _titleIcon.Opacity(opacity);
    _titleBarText.Opacity(opacity);
  });
  _efficiencyToggle.IsChecked(g_App.Settings.Current.EfficiencyModeForBackground);
  _efficiencyToggle.Click([this](auto &&, auto &&)
  {
    if (_current)
    {
      const IReference<bool> checked = _efficiencyToggle.IsChecked();
      _current->SetEfficiencyMode(checked && checked.Value());
    }
  });
  _primaryButton.Click([this](auto &&, auto &&) { OnPrimaryClick(); });
  _secondaryButton.Click([this](auto &&, auto &&) { OnSecondaryClick(); });
  _window.AppWindow().Closing([this](auto &&, auto &&)
  {
    _closed = true;
    if (_current && _current->IsActive())
      _current->Cancel();
  });
  _root.Loaded([this](auto &&, auto &&)
  {
    if (!_started)
    {
      _started = true;
      Run();
    }
  });
}

void COperationWindow::ResizeClient(int width, int height)
{
  const double scale = ::GetDpiForWindow(_hwnd) / 96.0;
  const Windowing::AppWindow appWindow = _window.AppWindow();
  appWindow.Resize(winrt::Windows::Graphics::SizeInt32 { (int32_t)(width * scale), (int32_t)(height * scale) });
  const winrt::Windows::Graphics::RectInt32 area = Windowing::DisplayArea::GetFromWindowId(appWindow.Id(),
      Windowing::DisplayAreaFallback::Nearest).WorkArea();
  const winrt::Windows::Graphics::SizeInt32 size = appWindow.Size();
  appWindow.Move(winrt::Windows::Graphics::PointInt32 { area.X + (area.Width - size.Width) / 2, area.Y + (area.Height - size.Height) / 2 });
}

void COperationWindow::SetButtonText(Button &button, const wchar_t *text)
{
  button.Content(box_value(text));
}


// ---------------- flow ----------------

fire_and_forget COperationWindow::Run()
{
  const UStringVector &paths = _request.Paths;
  if (paths.IsEmpty())
  {
    _window.Close();
    co_return;
  }
  try
  {
    const NLaunchVerb::EEnum verb = _request.Verb;
    if (verb == NLaunchVerb::kExtract)
    {
      ShowExtractOptions();
      co_return; // continues in ContinueExtract()
    }
    if (verb == NLaunchVerb::kExtractHere)
    {
      FOR_VECTOR (i, paths)
      {
        const FString dir = us2fs(GetDirectory(paths[i]));
        if (!co_await ExtractOne(paths[i], [dir](const CArchiveSession &) { return dir; }, false, CExtractOptions(), -1))
          break;
      }
    }
    else if (verb == NLaunchVerb::kExtractSmart)
    {
      FOR_VECTOR (i, paths)
      {
        const FString dir = us2fs(GetDirectory(paths[i]));
        if (!co_await ExtractOne(paths[i], [dir](const CArchiveSession &s) { return NExtractPlanner::GetSmartDestination(s, dir); },
            false, CExtractOptions(), -1))
          break;
      }
    }
    else if (verb == NLaunchVerb::kTest)
    {
      FOR_VECTOR (i, paths)
        if (!co_await TestOne(paths[i]))
          break;
    }
    else if (verb == NLaunchVerb::kCompress7z)
      co_await Compress(paths, UString(L"7z"), false);
    else if (verb == NLaunchVerb::kCompressZip)
      co_await Compress(paths, UString(L"zip"), false);
    else if (verb == NLaunchVerb::kCompressShare)
      co_await Compress(paths, UString(L"zip"), true);
  }
  catch (const hresult_error &e)
  {
    COperationMessage &m = _messages.AddNew();
    m.Severity = NMessageSeverity::kError;
    m.Text = ToUString(e.message());
  }
  Finish();
}


void COperationWindow::ShowExtractOptions()
{
  const UStringVector &archives = _request.Paths;
  const UString &first = archives[0];
  const UString folderName = NExtractPlanner::GetFolderNameFor(GetFileName(first));
  _optionsPanel = CreateExtractOptionsPanel(this, us2fs(GetDirectory(first)), folderName,
      g_App.Settings.Current.SmartExtractFolder, false);
  UString title (L"Extract ");
  if (archives.Size() == 1)
    title += GetFileName(first);
  else
  {
    title.Add_UInt32(archives.Size());
    title += " archives";
  }
  _titleText.Text(ToHString(title));
  if (!_optionsPanel.IsEmpty())
  {
    _optionsHost.Content(_optionsPanel->GetRoot());
    _optionsHost.Visibility(Visibility::Visible);
  }
  _efficiencyToggle.Visibility(Visibility::Collapsed);
  SetButtonText(_primaryButton, L"Extract");
  SetButtonText(_secondaryButton, L"Cancel");
  _optionsMode = true;
  ResizeClient(620, 600);
}


fire_and_forget COperationWindow::ContinueExtract()
{
  const UStringVector archives = _request.Paths;
  CExtractOptions options;
  bool createSubfolder = false;
  bool openFolder = false;
  if (!_optionsPanel.IsEmpty())
  {
    _optionsPanel->SaveDefaults();
    _optionsPanel->GetOptions(UString(), options);
    createSubfolder = _optionsPanel->GetCreateSubfolder();
    openFolder = _optionsPanel->GetOpenFolderWhenDone();
  }
  _optionsHost.Visibility(Visibility::Collapsed);
  _efficiencyToggle.Visibility(Visibility::Visible);
  ResizeClient(520, 340);

  try
  {
    FOR_VECTOR (i, archives)
    {
      // Several archives with "new folder" each get their own folder under the chosen parent.
      FString dest = options.Destination;
      if (createSubfolder && archives.Size() > 1)
      {
        UString d = fs2us(options.Destination);
        while (d.Len() > 3 && IS_PATH_SEPAR(d.Back()))
          d.DeleteBack();
        dest = us2fs(CombinePath(GetDirectory(d), NExtractPlanner::GetFolderNameFor(GetFileName(archives[i]))));
      }
      if (!co_await ExtractOne(archives[i], [dest](const CArchiveSession &) { return dest; }, true, options, openFolder ? 1 : 0))
        break;
    }
  }
  catch (const hresult_error &e)
  {
    COperationMessage &m = _messages.AddNew();
    m.Severity = NMessageSeverity::kError;
    m.Text = ToUString(e.message());
  }
  Finish();
}


IAsyncOperation<bool> COperationWindow::ExtractOne(UString archive, CDestinationFunc destination,
    bool useTemplate, CExtractOptions options, int openFolder)
{
  const CAppSettings &s = g_App.Settings.Current;
  if (!useTemplate)
  {
    options.Overwrite = s.OverwriteMode;
    options.PathMode = NExtractPathMode::kFullPaths;
    options.ZonePropagation = s.ZonePropagation;
    options.RestoreTimestamps = true;
    options.RestoreSymbolicLinks = s.RestoreSymbolicLinks;
  }
  UString title (L"Extracting ");
  title += GetFileName(archive);
  com_ptr<COperationViewModel> op = Begin(NOperationKind::kExtract, title, archive);
  CExtractResult result;
  const HRESULT hr = co_await _operations.RunAsync(op,
      [&](COperationContext &ctx, UString &err) -> HRESULT
      {
        CArchiveSession session;
        CArchiveOpenOptions oo;
        if (options.Password_Defined)
        {
          oo.Password_Defined = true;
          oo.Password = options.Password;
        }
        HRESULT res;
        // the prompts show "wrong password, try again" on the next request
        for (unsigned attempt = 0;; attempt++)
        {
          res = session.Open(us2fs(archive), oo, ctx, err);
          if (res != k_HRESULT_WrongPassword || attempt >= 4)
            break;
          err.Empty();
        }
        RINOK(res)
        options.Destination = destination(session);
        return NExtractEngine::Extract(session, NULL, options, ctx, result, err);
      },
      [&]
      {
        if (result.Errors == 0)
        {
          UString d (L"Extracted ");
          d += NPropertyFormatter::FormatCount(result.FilesProcessed, L"file");
          return d;
        }
        return NPropertyFormatter::FormatCount(result.Errors, L"error");
      },
      [&] { return fs2us(result.Destination); });
  End(*op);
  if (hr != S_OK)
    co_return false;
  _revealPath = fs2us(result.TopLevelPaths.Size() == 1 ? result.TopLevelPaths[0] : result.Destination);
  const bool open = openFolder < 0 ? s.OpenFolderAfterExtract : (openFolder != 0);
  if (open && result.Errors == 0)
  {
    const unsigned n = result.TopLevelPaths.Size();
    if (n > 0 && n <= 16)
    {
      UStringVector items;
      for (unsigned i = 0; i < n; i++)
        items.Add(fs2us(result.TopLevelPaths[i]));
      NShellActions::RevealInExplorer(items);
    }
    else
      NShellActions::OpenFolder(fs2us(result.Destination));
  }
  co_return true;
}


IAsyncOperation<bool> COperationWindow::TestOne(UString archive)
{
  UString title (L"Testing ");
  title += GetFileName(archive);
  com_ptr<COperationViewModel> op = Begin(NOperationKind::kTest, title, archive);
  CExtractResult result;
  const HRESULT hr = co_await _operations.RunAsync(op,
      [&](COperationContext &ctx, UString &err) -> HRESULT
      {
        CArchiveSession session;
        CArchiveOpenOptions oo;
        HRESULT res;
        for (unsigned attempt = 0;; attempt++)
        {
          res = session.Open(us2fs(archive), oo, ctx, err);
          if (res != k_HRESULT_WrongPassword || attempt >= 4)
            break;
          err.Empty();
        }
        RINOK(res)
        return NExtractEngine::Test(session, NULL, ctx, result, err);
      },
      [&]
      {
        if (result.Errors == 0)
        {
          UString d (L"No errors \x00B7 ");
          d += NPropertyFormatter::FormatCount(result.FilesProcessed, L"file");
          return d;
        }
        return NPropertyFormatter::FormatCount(result.Errors, L"error");
      });
  End(*op);
  co_return hr == S_OK;
}


IAsyncAction COperationWindow::Compress(UStringVector sources, UString format, bool share)
{
  UString first = sources[0];
  while (first.Len() > 3 && IS_PATH_SEPAR(first.Back()))
    first.DeleteBack();
  UString folder = GetDirectory(first);
  if (folder.IsEmpty())
    folder = first;
  UString stem;
  if (sources.Size() == 1)
  {
    stem = GetFileName(first);
    if (!NFind::DoesDirExist_FollowLink(us2fs(first)))
    {
      const int dot = stem.ReverseFind_Dot();
      if (dot > 0)
        stem.DeleteFrom((unsigned)dot);
    }
  }
  else
  {
    UString f (folder);
    while (f.Len() > 3 && IS_PATH_SEPAR(f.Back()))
      f.DeleteBack();
    stem = GetFileName(f);
    if (stem.Len() == 2 && stem[1] == ':')
      stem.Empty();
  }
  if (stem.IsEmpty())
    stem = L"Archive";
  UString name (stem);
  name.Add_Char('.');
  name += format;
  const UString target = share ? CombinePath(fs2us(NTempFiles::NewFolder()), name) : CombinePath(folder, name);

  CCompressionSettings settings;
  settings.Format = format;
  settings.Level = g_App.Settings.Current.DefaultLevel;
  const bool update = !share && NFind::DoesFileExist_FollowLink(us2fs(target));
  UString title (update ? L"Adding to " : L"Creating ");
  title += GetFileName(target);
  com_ptr<COperationViewModel> op = Begin(update ? NOperationKind::kUpdate : NOperationKind::kCompress, title, target);
  FStringVector src;
  FOR_VECTOR (i, sources)
    src.Add(us2fs(sources[i]));
  CCompressResult result;
  const HRESULT hr = co_await _operations.RunAsync(op,
      [&](COperationContext &ctx, UString &err) -> HRESULT
      {
        if (!update)
          return NArchiveWriter::Create(us2fs(target), src, settings, ctx, result, err);
        CArchiveSession session;
        RINOK(session.Open(us2fs(target), CArchiveOpenOptions(), ctx, err))
        CUpdatePlan plan;
        plan.Add = src;
        return NArchiveWriter::Update(session, plan, &settings, ctx, result, err);
      },
      [&]
      {
        UString d = NPropertyFormatter::FormatSize(result.OutputBytes);
        d += L" \x00B7 ";
        d += FormatPercent(result.GetRatio());
        d += " of original";
        return d;
      },
      [&] { return result.OutputFiles.IsEmpty() ? UString() : fs2us(result.OutputFiles[0]); });
  End(*op);
  if (hr != S_OK)
    co_return;
  _revealPath = result.OutputFiles.IsEmpty() ? UString() : fs2us(result.OutputFiles[0]);
  if (share)
  {
    UStringVector files;
    FOR_VECTOR (i, result.OutputFiles)
      files.Add(fs2us(result.OutputFiles[i]));
    NShareService::ShareFiles(_hwnd, files, name);
    _revealPath.Empty();
  }
}


// ---------------- progress UI ----------------

com_ptr<COperationViewModel> COperationWindow::Begin(NOperationKind::EEnum kind, const UString &title, const UString &archivePath)
{
  com_ptr<COperationViewModel> op = make_self<COperationViewModel>(kind, title, &_dialogs,
      kind == NOperationKind::kCompress ? UString() : archivePath);
  const IReference<bool> checked = _efficiencyToggle.IsChecked();
  op->SetEfficiencyMode(checked && checked.Value());
  _current = op;
  _titleText.Text(ToHString(title));
  _progressPanel.Visibility(Visibility::Visible);
  _progress.IsIndeterminate(true);
  _progress.Value(0);
  _detailText.Text(L"Preparing\x2026");
  _statsText.Text(L"");
  SetButtonText(_primaryButton, L"Cancel");
  SetButtonText(_secondaryButton, L"Pause");
  _secondaryButton.Visibility(Visibility::Visible);
  _progressToken = op->ProgressChanged([this] { OnProgress(); });
  _window.AppWindow().Title(ToHString(title));
  return op;
}

void COperationWindow::OnProgress()
{
  if (!_current)
    return;
  const COperationViewModel &op = *_current;
  _progress.IsIndeterminate(op.GetIsIndeterminate());
  _progress.Value(op.GetPercent());
  _detailText.Text(ToHString(op.GetDetail()));
  _statsText.Text(ToHString(op.GetStats()));
  UString title;
  if (!op.GetIsIndeterminate())
  {
    title.Add_UInt32((UInt32)(op.GetPercent() + 0.5));
    title += L"% \x00B7 ";
  }
  title += op.GetTitle();
  _window.AppWindow().Title(ToHString(title));
}

void COperationWindow::End(COperationViewModel &op)
{
  op.ProgressChanged(_progressToken);
  _messages += op.Messages;
  if (op.GetState() == NOperationState::kFailed)
  {
    COperationMessage &m = _messages.AddNew();
    m.Severity = NMessageSeverity::kError;
    m.Text = op.GetSummary();
    m.ItemPath = op.GetTitle();
  }
  else if (op.GetState() == NOperationState::kCancelled && !HasCancelledMessage(op.Messages))
  {
    COperationMessage &m = _messages.AddNew();
    m.Severity = NMessageSeverity::kInfo;
    m.Text = kCancelledText;
    m.ItemPath = op.GetTitle();
  }
  _statsText.Text(ToHString(op.GetSummary()));
  _current = nullptr;
}

fire_and_forget COperationWindow::Finish()
{
  _finished = true;
  _progressPanel.Visibility(Visibility::Collapsed);
  _efficiencyToggle.Visibility(Visibility::Collapsed);
  unsigned errors = 0, warnings = 0;
  bool cancelled = false;
  FOR_VECTOR (i, _messages)
  {
    const COperationMessage &m = _messages[i];
    if (m.Severity == NMessageSeverity::kError)
      errors++;
    else if (m.Severity == NMessageSeverity::kWarning)
      warnings++;
    if (m.Text == kCancelledText)
      cancelled = true;
  }

  const NLaunchVerb::EEnum verb = _request.Verb;
  _resultBar.Severity(errors > 0 ? InfoBarSeverity::Error : warnings > 0 ? InfoBarSeverity::Warning
      : cancelled ? InfoBarSeverity::Informational : InfoBarSeverity::Success);
  UString title;
  if (errors > 0)
  {
    title = L"Finished with ";
    title += NPropertyFormatter::FormatCount(errors, L"error");
  }
  else if (cancelled)
    title = L"Cancelled";
  else if (verb == NLaunchVerb::kTest)
    title = L"No errors found";
  else if (verb == NLaunchVerb::kCompressShare)
    title = L"Ready to share";
  else
    title = L"Done";
  _resultBar.Title(ToHString(title));
  _resultBar.Message(_statsText.Text());
  _resultBar.IsOpen(true);
  UString windowTitle (title);
  windowTitle += L" \x2013 7-Zip";
  _window.AppWindow().Title(ToHString(windowTitle));

  if (!_messages.IsEmpty() && !(cancelled && _messages.Size() == 1))
  {
    const unsigned num = _messages.Size() < 300 ? _messages.Size() : 300;
    for (unsigned i = 0; i < num; i++)
    {
      const COperationMessage &m = _messages[i];
      UString text;
      if (!m.ItemPath.IsEmpty())
      {
        text = m.ItemPath;
        text += ": ";
      }
      text += m.Text;
      TextBlock t;
      t.Text(ToHString(text));
      t.TextWrapping(TextWrapping::Wrap);
      t.IsTextSelectionEnabled(true);
      _messageList.Items().Append(t);
    }
    _messageList.Visibility(Visibility::Visible);
    ResizeClient(560, 460);
  }

  SetButtonText(_primaryButton, L"Close");
  SetButtonText(_secondaryButton, L"Show in File Explorer");
  _secondaryButton.Visibility(_revealPath.IsEmpty() ? Visibility::Collapsed : Visibility::Visible);

  // Like the classic progress dialog: a clean, successful job just disappears.
  const bool autoClose = errors == 0 && warnings == 0 && !cancelled
      && verb != NLaunchVerb::kTest && verb != NLaunchVerb::kCompressShare;
  if (autoClose)
  {
    co_await resume_after(std::chrono::milliseconds(1500));
    co_await ResumeForeground(_dialogs.GetDispatcher());
    if (!_closed)
      _window.Close();
  }
}


// ---------------- buttons ----------------

void COperationWindow::OnPrimaryClick()
{
  if (_optionsMode)
  {
    if (!_optionsPanel.IsEmpty() && !_optionsPanel->ValidateAndShowError())
      return;
    _optionsMode = false;
    ContinueExtract();
  }
  else if (_current && _current->IsActive())
    _current->Cancel();
  else if (_finished)
    _window.Close();
}

void COperationWindow::OnSecondaryClick()
{
  if (_optionsMode)
  {
    _optionsMode = false;
    _window.Close();
  }
  else if (_current && _current->IsActive())
  {
    _current->TogglePause();
    SetButtonText(_secondaryButton, _current->IsPaused() ? L"Resume" : L"Pause");
  }
  else if (_finished && !_revealPath.IsEmpty())
  {
    const NLaunchVerb::EEnum verb = _request.Verb;
    if (NFind::DoesDirExist_FollowLink(us2fs(_revealPath))
        && verb != NLaunchVerb::kCompress7z && verb != NLaunchVerb::kCompressZip)
      NShellActions::OpenFolder(_revealPath);
    else
      NShellActions::RevealInExplorer(_revealPath);
    _window.Close();
  }
}


// ---------------- IWindowHost (interactive work goes to the main window) ----------------

void COperationWindow::LaunchMain(const char *verb, const UStringVector &paths)
{
  UString exe = fs2us(NDLL::GetModuleDirPrefix());
  exe += "SevenZip.exe";
  UString args (verb);
  FOR_VECTOR (i, paths)
  {
    args += " \"";
    args += paths[i];
    args += "\"";
  }
  CProcess process;
  process.Create(exe, args, NULL);
}

HWND COperationWindow::GetHwnd() { return _hwnd; }
WindowId COperationWindow::GetWindowId() { return _window.AppWindow().Id(); }
CDialogService &COperationWindow::Dialogs() { return _dialogs; }
COperationService &COperationWindow::Operations() { return _operations; }

void COperationWindow::OpenArchive(const UString &path, const UString &)
{
  UStringVector v;
  v.Add(path);
  LaunchMain("open", v);
}

void COperationWindow::OpenNestedArchive(CArchiveViewModel *, const UString &extractedPath, const UString &)
{
  UStringVector v;
  v.Add(extractedPath);
  LaunchMain("open", v);
}

void COperationWindow::OpenCompress(const UStringVector &paths, const UString &) { LaunchMain("compress", paths); }
void COperationWindow::OpenHash(const UStringVector &paths, const UString &) { LaunchMain("hash", paths); }
void COperationWindow::OpenBenchmark() { LaunchMain("benchmark", UStringVector()); }
void COperationWindow::OpenSettings() { LaunchMain("settings", UStringVector()); }
void COperationWindow::UpdateTab(ITabPage *, const UString &) {}
void COperationWindow::CloseTab(ITabPage *) {}
