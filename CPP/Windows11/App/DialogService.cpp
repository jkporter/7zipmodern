// DialogService.cpp

#include "StdAfx.h"

#include "DialogService.h"

#include "../../Common/Defs.h"

#include "../../Windows/Synchronization.h"

#include "../Dispatcher.h"
#include "../HString.h"

#include "../Core/PropertyFormatter.h"

#include "App.h"
#include "PlatformServices.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;
using namespace NWindows11;

static IInspectable FindResource(const wchar_t *key)
{
  return Application::Current().Resources().TryLookup(box_value(key));
}

static Media::Brush FindBrush(const wchar_t *key)
{
  const IInspectable v = FindResource(key);
  return v ? v.try_as<Media::Brush>() : nullptr;
}

static TextBlock MakeText(const UString &text, bool selectable = false)
{
  TextBlock t;
  t.Text(ToHString(text));
  t.TextWrapping(TextWrapping::Wrap);
  if (selectable)
    t.IsTextSelectionEnabled(true);
  return t;
}


// ---------- gate: one dialog at a time (all on the UI thread) ----------

bool CDialogService::CGateAwaiter::await_ready()
{
  if (Service->_busy)
    return false;
  Service->_busy = true;
  return true;
}

void CDialogService::CGateAwaiter::await_suspend(std::coroutine_handle<> handle)
{
  Service->_waiters.push_back(handle);
}

void CDialogService::LeaveGate()
{
  if (_waiters.empty())
  {
    _busy = false;
    return;
  }
  // the gate stays taken and passes to the next waiter
  const std::coroutine_handle<> next = _waiters.front();
  _waiters.erase(_waiters.begin());
  if (!_dispatcher.TryEnqueue([next] { next(); }))
    _busy = false;
}


void CDialogService::Init(const UIElement &root, HWND hwnd)
{
  _root = root;
  _hwnd = hwnd;
  _dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
}


IAsyncOperation<ContentDialogResult> CDialogService::ShowAsync(ContentDialog dialog)
{
  co_await ResumeForeground(_dispatcher);
  co_await EnterGate();
  ContentDialogResult result = ContentDialogResult::None;
  try
  {
    const XamlRoot xamlRoot = _root.XamlRoot();
    dialog.XamlRoot(xamlRoot);
    if (!dialog.Style())
    {
      const IInspectable style = FindResource(L"DefaultContentDialogStyle");
      if (style)
        dialog.Style(style.as<Style>());
    }
    if (xamlRoot)
    {
      const FrameworkElement fe = xamlRoot.Content().try_as<FrameworkElement>();
      if (fe)
        dialog.RequestedTheme(fe.ActualTheme());
    }
    result = co_await dialog.ShowAsync();
  }
  catch (...)
  {
    result = ContentDialogResult::None;
  }
  LeaveGate();
  co_return result;
}


IAsyncOperation<bool> CDialogService::ConfirmAsync(UString title, UString message, UString primary, bool destructive)
{
  co_await ResumeForeground(_dispatcher);
  ContentDialog d;
  d.Title(box_value(ToHString(title)));
  d.Content(MakeText(message));
  d.PrimaryButtonText(ToHString(primary));
  d.CloseButtonText(L"Cancel");
  d.DefaultButton(destructive ? ContentDialogButton::Close : ContentDialogButton::Primary);
  const ContentDialogResult r = co_await ShowAsync(d);
  co_return r == ContentDialogResult::Primary;
}


IAsyncAction CDialogService::ShowErrorAsync(UString title, UString message)
{
  co_await ResumeForeground(_dispatcher);
  ContentDialog d;
  d.Title(box_value(ToHString(title)));
  d.Content(MakeText(message, true));
  d.CloseButtonText(L"Close");
  d.DefaultButton(ContentDialogButton::Close);
  co_await ShowAsync(d);
}


IAsyncOperation<hstring> CDialogService::PromptTextAsync(UString title, UString label, UString initial,
    UString primary, CTextValidator validate)
{
  co_await ResumeForeground(_dispatcher);
  TextBox box;
  box.Header(box_value(ToHString(label)));
  box.Text(ToHString(initial));
  box.MinWidth(360);
  TextBlock error;
  error.Foreground(FindBrush(L"SystemFillColorCriticalBrush"));
  error.Visibility(Visibility::Collapsed);
  error.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
  error.TextWrapping(TextWrapping::Wrap);
  StackPanel panel;
  panel.Children().Append(box);
  panel.Children().Append(error);

  ContentDialog d;
  d.Title(box_value(ToHString(title)));
  d.Content(panel);
  d.PrimaryButtonText(ToHString(primary));
  d.CloseButtonText(L"Cancel");
  d.DefaultButton(ContentDialogButton::Primary);

  const int dot = initial.ReverseFind_Dot();
  const int selLen = dot > 0 ? dot : (int)initial.Len();
  box.Loaded([box, selLen](const IInspectable &, const RoutedEventArgs &)
  {
    box.Focus(FocusState::Programmatic);
    box.Select(0, selLen);
  });
  box.TextChanged([box, error, d, validate](const IInspectable &, const TextChangedEventArgs &)
  {
    const UString text = ToUString(box.Text());
    UString msg;
    if (validate)
      msg = validate(text);
    error.Text(ToHString(msg));
    error.Visibility(msg.IsEmpty() ? Visibility::Collapsed : Visibility::Visible);
    UString trimmed (text);
    trimmed.Trim();
    d.IsPrimaryButtonEnabled(msg.IsEmpty() && !trimmed.IsEmpty());
  });

  const ContentDialogResult r = co_await ShowAsync(d);
  if (r != ContentDialogResult::Primary)
    co_return hstring();
  UString text = ToUString(box.Text());
  text.Trim();
  co_return ToHString(text);
}


IAsyncAction CDialogService::ShowMessagesAsync(UString title, CObjectVector<COperationMessage> messages)
{
  co_await ResumeForeground(_dispatcher);
  ListView list;
  list.SelectionMode(ListViewSelectionMode::None);
  list.MaxHeight(360);
  const unsigned num = MyMin(messages.Size(), (unsigned)500);
  for (unsigned i = 0; i < num; i++)
  {
    const COperationMessage &m = messages[i];
    const wchar_t *glyph = m.Severity == NMessageSeverity::kError ? L"\xE783" :
        m.Severity == NMessageSeverity::kWarning ? L"\xE7BA" : L"\xE946";
    const wchar_t *brush = m.Severity == NMessageSeverity::kError ? L"SystemFillColorCriticalBrush" :
        m.Severity == NMessageSeverity::kWarning ? L"SystemFillColorCautionBrush" : L"TextFillColorSecondaryBrush";
    Grid row;
    row.ColumnSpacing(10);
    row.Padding(ThicknessHelper::FromLengths(0, 4, 0, 4));
    ColumnDefinition c0;
    c0.Width(GridLengthHelper::Auto());
    row.ColumnDefinitions().Append(c0);
    row.ColumnDefinitions().Append(ColumnDefinition());
    FontIcon icon;
    icon.Glyph(glyph);
    icon.FontSize(16);
    icon.Foreground(FindBrush(brush));
    icon.VerticalAlignment(VerticalAlignment::Top);
    row.Children().Append(icon);
    StackPanel text;
    if (!m.ItemPath.IsEmpty())
    {
      TextBlock path;
      path.Text(ToHString(m.ItemPath));
      const IInspectable style = FindResource(L"BodyStrongTextBlockStyle");
      if (style)
        path.Style(style.as<Style>());
      path.TextTrimming(TextTrimming::CharacterEllipsis);
      text.Children().Append(path);
    }
    text.Children().Append(MakeText(m.Text, true));
    Grid::SetColumn(text, 1);
    row.Children().Append(text);
    list.Items().Append(row);
  }
  ContentDialog d;
  d.Title(box_value(ToHString(title)));
  d.Content(list);
  d.CloseButtonText(L"Close");
  d.DefaultButton(ContentDialogButton::Close);
  co_await ShowAsync(d);
}


// ---------- CUiPrompts ----------

struct CPromptState
{
  NWindows::NSynchronization::CManualResetEvent Done;
  HRESULT Result;
  UString Password;
  NOverwriteChoice::EEnum Choice;
  CPromptState(): Result(E_ABORT), Choice(NOverwriteChoice::kCancel) { Done.Create(); }
};

static fire_and_forget ShowPasswordDialog(CDialogService *dialogs, std::shared_ptr<CPromptState> state,
    UString archiveName, UString archivePath, bool retry, bool tryVault)
{
  try
  {
    if (tryVault)
    {
      const hstring saved = co_await NPasswordStore::TryUnlockAsync(dialogs->GetHwnd(), archivePath);
      if (!saved.empty())
      {
        state->Password = ToUString(saved);
        state->Result = S_OK;
        state->Done.Set();
        co_return;
      }
    }

    PasswordBox box;
    box.Header(box_value(L"Password"));
    box.MinWidth(340);
    box.PasswordRevealMode(PasswordRevealMode::Peek);
    // A wrong password is a validation error, so it's reported inline under the field.
    TextBlock error = MakeText(UString(L"That password didn't work. Check it and try again."));
    error.Foreground(FindBrush(L"SystemFillColorCriticalBrush"));
    error.Visibility(retry ? Visibility::Visible : Visibility::Collapsed);
    Automation::AutomationProperties::SetLiveSetting(error, Automation::Peers::AutomationLiveSetting::Assertive);
    CheckBox remember;
    remember.Content(box_value(L"Remember for this archive (unlocked with Windows Hello)"));
    remember.Visibility(archivePath.IsEmpty() ? Visibility::Collapsed : Visibility::Visible);

    UString intro (L"\x201C");
    intro += archiveName;
    intro += L"\x201D is encrypted.";
    StackPanel panel;
    panel.Spacing(12);
    panel.Children().Append(MakeText(intro));
    panel.Children().Append(box);
    panel.Children().Append(error);
    panel.Children().Append(remember);

    ContentDialog d;
    d.Title(box_value(L"Enter password"));
    d.Content(panel);
    d.PrimaryButtonText(L"Unlock");
    d.CloseButtonText(L"Cancel");
    d.DefaultButton(ContentDialogButton::Primary);
    box.Loaded([box](const IInspectable &, const RoutedEventArgs &) { box.Focus(FocusState::Programmatic); });

    const ContentDialogResult r = co_await dialogs->ShowAsync(d);
    if (r == ContentDialogResult::Primary)
    {
      state->Password = ToUString(box.Password());
      state->Result = S_OK;
      const IReference<bool> checked = remember.IsChecked();
      if (checked && checked.Value() && !archivePath.IsEmpty())
        NPasswordStore::Save(archivePath, state->Password);
    }
  }
  catch (...)
  {
    state->Result = E_ABORT;
  }
  state->Done.Set();
}


static UString DescribeFile(const wchar_t *label, UInt64 size, bool modifiedDefined, const FILETIME &modified)
{
  UString s (label);
  s.Add_LF();
  s += NPropertyFormatter::FormatSize(size);
  s += L" \x00B7 ";
  if (modifiedDefined)
    s += NPropertyFormatter::FormatFileTime(modified);
  else
    s += "unknown date";
  return s;
}

static fire_and_forget ShowOverwriteDialog(CDialogService *dialogs, std::shared_ptr<CPromptState> state,
    COverwriteRequest request)
{
  try
  {
    CheckBox applyAll;
    applyAll.Content(box_value(L"Do this for all conflicts"));
    RadioButtons choice;
    RadioButton r0, r1, r2;
    r0.Content(box_value(ToHString(DescribeFile(L"Replace with the file from the archive",
        request.NewSize, request.NewModified_Defined, request.NewModified))));
    r1.Content(box_value(ToHString(DescribeFile(L"Keep the file in the destination",
        request.ExistingSize, request.ExistingModified_Defined, request.ExistingModified))));
    r2.Content(box_value(L"Keep both (rename the extracted file)"));
    choice.Items().Append(r0);
    choice.Items().Append(r1);
    choice.Items().Append(r2);
    const bool newer = request.NewModified_Defined && request.ExistingModified_Defined
        && ::CompareFileTime(&request.NewModified, &request.ExistingModified) > 0;
    choice.SelectedIndex(newer ? 0 : 1);

    const FString &target = request.TargetPath;
    UString intro (L"The destination already has a file named \x201C");
    intro += fs2us(target).Ptr((unsigned)(target.ReverseFind_PathSepar() + 1));
    intro += L"\x201D.";
    StackPanel panel;
    panel.Spacing(12);
    panel.MaxWidth(460);
    panel.Children().Append(MakeText(intro));
    panel.Children().Append(choice);
    panel.Children().Append(applyAll);

    ContentDialog d;
    d.Title(box_value(L"Replace or skip file"));
    d.Content(panel);
    d.PrimaryButtonText(L"Continue");
    d.CloseButtonText(L"Cancel extraction");
    d.DefaultButton(ContentDialogButton::Primary);

    const ContentDialogResult r = co_await dialogs->ShowAsync(d);
    if (r == ContentDialogResult::Primary)
    {
      const IReference<bool> checked = applyAll.IsChecked();
      const bool all = checked && checked.Value();
      const int index = choice.SelectedIndex();
      state->Choice =
          index == 0 ? (all ? NOverwriteChoice::kReplaceAll : NOverwriteChoice::kReplace) :
          index == 1 ? (all ? NOverwriteChoice::kSkipAll : NOverwriteChoice::kSkip) :
                       (all ? NOverwriteChoice::kKeepBothAll : NOverwriteChoice::kKeepBoth);
      state->Result = S_OK;
    }
  }
  catch (...)
  {
    state->Result = E_ABORT;
  }
  state->Done.Set();
}


// The worker waits for the answer, but gives up when the operation is cancelled.
static HRESULT WaitForAnswer(CPromptState &state, const COperationContext *context)
{
  for (;;)
  {
    if (::WaitForSingleObject(state.Done, 200) == WAIT_OBJECT_0)
      return state.Result;
    if (context && context->IsCancelled())
      return E_ABORT;
  }
}


HRESULT CUiPrompts::RequestPassword(const UString &archiveName, bool retry, UString &password)
{
  password.Empty();
  const winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher = _dialogs->GetDispatcher();
  if (!dispatcher || dispatcher.HasThreadAccess())
    return E_ABORT; // the UI thread can't block on its own dialog
  if (_asked++ > 0)
    retry = true;
  const bool tryVault = !_triedVault && !_archivePath.IsEmpty() && g_App.Settings.Current.OfferSavedPasswords;
  if (tryVault)
    _triedVault = true;
  std::shared_ptr<CPromptState> state = std::make_shared<CPromptState>();
  CDialogService *dialogs = _dialogs;
  const UString archivePath = _archivePath;
  if (!dispatcher.TryEnqueue([dialogs, state, archiveName, archivePath, retry, tryVault]
      { ShowPasswordDialog(dialogs, state, archiveName, archivePath, retry, tryVault); }))
    return E_ABORT;
  RINOK(WaitForAnswer(*state, _context))
  password = state->Password;
  return S_OK;
}


HRESULT CUiPrompts::ResolveOverwrite(const COverwriteRequest &request, NOverwriteChoice::EEnum &choice)
{
  choice = NOverwriteChoice::kCancel;
  const winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher = _dialogs->GetDispatcher();
  if (!dispatcher || dispatcher.HasThreadAccess())
    return S_OK;
  std::shared_ptr<CPromptState> state = std::make_shared<CPromptState>();
  CDialogService *dialogs = _dialogs;
  if (!dispatcher.TryEnqueue([dialogs, state, request] { ShowOverwriteDialog(dialogs, state, request); }))
    return S_OK;
  if (WaitForAnswer(*state, _context) == S_OK)
    choice = state->Choice;
  return S_OK;
}
