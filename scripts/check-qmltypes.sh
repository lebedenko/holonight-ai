#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${BUILD_DIR:-build}}"
if [[ "${build_dir}" = /* ]]; then
  build_root="${build_dir}"
else
  build_root="${repo_root}/${build_dir}"
fi

chat_module_dir="${build_root}/apps/chat/HolonightChat"
qmltypes_file="${chat_module_dir}/HolonightChat.qmltypes"

required_types=(
  "ChatViewModel"
  "MessageListModel"
)

if [[ ! -s "${qmltypes_file}" ]]; then
  echo "Missing or empty qmltypes file: ${qmltypes_file}" >&2
  echo "Build holonight-chat before running this check." >&2
  exit 1
fi

if ! grep -q 'Module {' "${qmltypes_file}"; then
  echo "Malformed qmltypes file: ${qmltypes_file}" >&2
  exit 1
fi

missing_types=()
for type_name in "${required_types[@]}"; do
  # Classes are namespaced (e.g. name: "holonight_application::ChatViewModel"), so match on a
  # trailing "::TypeName" or a bare TypeName rather than an exact string.
  if ! grep -qE "name: \"([A-Za-z0-9_]+::)*${type_name}\"" "${qmltypes_file}"; then
    missing_types+=("${type_name}")
  fi
done

if (( ${#missing_types[@]} > 0 )); then
  {
    echo "Generated qmltypes file is missing required HolonightChat types:"
    printf '  %s\n' "${missing_types[@]}"
    echo
    echo "Checked: ${qmltypes_file}"
  } >&2
  exit 1
fi

echo "QML type metadata check passed."
