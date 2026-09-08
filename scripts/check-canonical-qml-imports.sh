#!/usr/bin/env bash

set -euo pipefail

source_dir=${1:-.}
qml_dir="${source_dir}/qml"
failed=0

if rg -n '\bHnSelectableDelegate\b' "${qml_dir}"; then
  echo "Use public composite types and enums instead of internal HnSelectableDelegate" >&2
  failed=1
fi

core_types='\b(HoloniightPalette|HolonightTheme|HnAppearance|HnShapeProfile|HnSurfaceRole|HnCornerStyle|HnShapeKind|HnCornerMask|HnIconProvider|HnIcon)\b'
controls_types='\b(HnSurfaceFrame|HnApplicationWindow|HnSearchField|HnIconComboBox|HnTextArea|HnFormField|HnSettingsRow|HnSectionHeader|HnEmptyState|HnLoadingState|HnNavigationDelegate|HnListDelegate|HnCardDelegate|HnActionDelegate|HnStatusIndicator|HnKeyHint|HnPanelHeader|HnSegmentedControl|HnChoiceCard|HnActionBar|HnSeparator)\b'
runtime_types='ApplicationWindow|Label|ToolButton|ToolBar|ToolSeparator|MenuSeparator|Popup|MenuBar|MenuBarItem|Button|CheckBox|ComboBox|ItemDelegate|Menu|MenuItem|ProgressBar|RadioButton|ScrollBar|ScrollView|Slider|SpinBox|Switch|TabBar|TabButton|TextArea|TextField|ToolTip|Control|ButtonGroup|Overlay|RangeSlider|Frame|Pane|Page|Dialog|DialogButtonBox|BusyIndicator|SwipeView|StackView|Action|ActionGroup|RoundButton|DelayButton|Tumbler|SplitView'

while IFS= read -r qml_file; do
  if rg -q "${core_types}" "${qml_file}" && ! rg -q '^import Holonight\.Core($|[[:space:]])' "${qml_file}"; then
    echo "${qml_file}: migrated Core type used without import Holonight.Core" >&2
    failed=1
  fi

  if rg -q "${controls_types}" "${qml_file}" \
      && ! rg -q '^import Holonight\.Controls($|[[:space:]])' "${qml_file}"; then
    echo "${qml_file}: migrated Controls type used without import Holonight.Controls" >&2
    failed=1
  fi

  if rg -n '^import (Holonight($|[[:space:]])|QtQuick\.Controls\.)' "${qml_file}"; then
    echo "${qml_file}: direct style import is forbidden" >&2
    failed=1
  fi
  if rg '^import QtQuick\.Controls($|[[:space:]])' "${qml_file}" | rg -v '^import QtQuick\.Controls as Controls$'; then
    echo "${qml_file}: runtime import must use Controls namespace" >&2
    failed=1
  fi
  if rg -q '\bControls\.' "${qml_file}" && ! rg -q '^import QtQuick\.Controls as Controls$' "${qml_file}"; then
    echo "${qml_file}: Controls use requires file-local runtime import" >&2
    failed=1
  fi
  if rg -n "(^|[^.[:alnum:]_])(${runtime_types})([[:space:]]*\{|\.)" "${qml_file}"; then
    echo "${qml_file}: qualify control instances, enums and attached properties" >&2
    failed=1
  fi
done < <(find "${qml_dir}" -type f -name '*.qml' -print | sort)

if rg -n '^import holonight\.(core|controls)' "${qml_dir}"; then
  echo "lowercase canonical module import found" >&2
  failed=1
fi

if [[ -e "${qml_dir}/shared/HeaderComboBox.qml" ]] \
    || rg -n '\bHeaderComboBox[[:space:]]*\{' "${qml_dir}"; then
  echo "superseded local HeaderComboBox is still present or referenced" >&2
  failed=1
fi

for adoption in \
  'workspace/ConversationListPanel.qml:HnSearchField' \
  'shared/ChatHeader.qml:HnIconComboBox' \
  'workspace/SettingsSidebar.qml:HnNavigationDelegate' \
  'workspace/ProviderListDelegate.qml:HnListDelegate' \
  'quickpanel/QuickPanelHeader.qml:HnActionDelegate'; do
  relative_file=${adoption%%:*}
  control=${adoption##*:}
  if ! rg -q "\\b${control}[[:space:]]*\\{" "${qml_dir}/${relative_file}"; then
    echo "${qml_dir}/${relative_file}: expected shared ${control} adoption" >&2
    failed=1
  fi
done

for selection_list in \
  'workspace/ConversationListPanel.qml' \
  'workspace/ProviderListPanel.qml'; do
  if ! rg -q 'currentIndex:[[:space:]]*-1' "${qml_dir}/${selection_list}"; then
    echo "${qml_dir}/${selection_list}: semantic selection list must disable implicit current-item selection" >&2
    failed=1
  fi
done

exit "${failed}"
