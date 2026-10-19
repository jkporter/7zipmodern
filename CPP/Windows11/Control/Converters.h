// Windows11/Control/Converters.h

#ifndef ZIP7_INC_WINDOWS11_CONTROL_CONVERTERS_H
#define ZIP7_INC_WINDOWS11_CONTROL_CONVERTERS_H

#include "../WinUI.h"

namespace NWindows11 {
namespace NControl {

/*
Value converters for {Binding}, added to a ResourceDictionary under these keys:
  VisibleIf          bool -> Visible / Collapsed
  CollapsedIf        bool -> Collapsed / Visible
  VisibleIfNotEmpty  string, number or object -> Collapsed when empty, zero or null
  VisibleIfEmpty     the inverse of VisibleIfNotEmpty
  Not                bool -> !bool
Use them as {Binding IsBusy, Converter={StaticResource VisibleIf}}.
*/

void AddConverters(const winrt::Microsoft::UI::Xaml::ResourceDictionary &resources);

}}

#endif
