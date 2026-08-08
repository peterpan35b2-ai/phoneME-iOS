#!/usr/bin/env bash

# Shared helpers for isolated phoneME test/build scripts.
# This file is sourced by the scripts in Core/Tools.

phoneme_tool_error() {
  printf 'phoneME tooling error: %s\n' "$*" >&2
}

phoneme_sanitize_task_id() {
  local value="${1:-task}"
  value="${value//[^A-Za-z0-9_.-]/_}"
  [[ -n "$value" ]] || value="task"
  printf '%s\n' "${value:0:64}"
}

phoneme_assert_safe_path() {
  local path="${1:-}"
  local core_root="${2:-${CORE_ROOT:-}}"

  [[ -n "$path" ]] || {
    phoneme_tool_error 'refusing an empty path'
    return 64
  }
  [[ "$path" == /* ]] || {
    phoneme_tool_error "path must be absolute: $path"
    return 64
  }

  case "$path" in
    /|/System|/Library|/Applications|/Users|/Volumes|/private|/tmp|/var)
      phoneme_tool_error "refusing unsafe path: $path"
      return 64
      ;;
  esac

  if [[ -n "${HOME:-}" && "$path" == "$HOME" ]]; then
    phoneme_tool_error "refusing HOME as a build path: $path"
    return 64
  fi
  if [[ -n "$core_root" ]]; then
    case "$path" in
      "$core_root"|"$core_root/build")
        phoneme_tool_error "refusing shared project path: $path"
        return 64
        ;;
    esac
  fi
}

phoneme_resolve_path_for_creation() {
  local path="$1"
  local cursor="$path"
  local suffix=""
  local part parent

  while [[ ! -e "$cursor" ]]; do
    part="$(basename "$cursor")"
    suffix="/$part$suffix"
    parent="$(dirname "$cursor")"
    [[ "$parent" != "$cursor" ]] || break
    cursor="$parent"
  done

  [[ -d "$cursor" ]] || {
    phoneme_tool_error "path ancestor is not a directory: $cursor"
    return 64
  }
  cursor="$(cd "$cursor" && pwd -P)"
  printf '%s%s\n' "$cursor" "$suffix"
}

phoneme_make_isolated_root() {
  local core_root="$1"
  local label="$2"
  local override_base="${3:-}"
  local base task template root

  if [[ -n "$override_base" ]]; then
    base="$override_base"
  elif [[ -n "${PHONEME_BUILD_ROOT:-}" ]]; then
    base="$PHONEME_BUILD_ROOT"
  else
    base="$core_root/build/test-runs"
  fi

  [[ "$base" == /* ]] || base="$PWD/$base"
  [[ ! -L "$base" ]] || {
    phoneme_tool_error "refusing symlink build base: $base"
    return 64
  }
  base="$(phoneme_resolve_path_for_creation "$base")" || return
  phoneme_assert_safe_path "$base" "$core_root" || return
  mkdir -p "$base"
  base="$(cd "$base" && pwd -P)"
  phoneme_assert_safe_path "$base" "$core_root" || return

  task="$(phoneme_sanitize_task_id "${PHONEME_TASK_ID:-task}")"
  label="$(phoneme_sanitize_task_id "$label")"
  template="$base/${label}.${task}.$$.XXXXXX"
  root="$(mktemp -d "$template")" || {
    phoneme_tool_error "unable to create isolated root under $base"
    return 1
  }

  printf '%s\n' "phoneME isolated root" > "$root/.phoneme-test-root"
  printf '%s\n' "$root"
}

phoneme_prepare_managed_root() {
  local path="$1"
  local core_root="$2"

  [[ "$path" == /* ]] || path="$PWD/$path"
  [[ ! -L "$path" ]] || {
    phoneme_tool_error "refusing symlink managed root: $path"
    return 64
  }
  path="$(phoneme_resolve_path_for_creation "$path")" || return
  phoneme_assert_safe_path "$path" "$core_root" || return

  if [[ -e "$path" ]]; then
    [[ -f "$path/.phoneme-test-root" ]] || {
      phoneme_tool_error "refusing to replace unmarked directory: $path"
      return 65
    }
    phoneme_safe_remove_root "$path" "$core_root" || return
  fi

  mkdir -p "$path"
  path="$(cd "$path" && pwd -P)"
  phoneme_assert_safe_path "$path" "$core_root" || return
  printf '%s\n' "phoneME isolated root" > "$path/.phoneme-test-root"
  printf '%s\n' "$path"
}

# Build caches need a managed root with the same path-safety guarantees as test
# roots, but deleting it on every invocation defeats incremental compilation.
# Reuse only directories previously marked by our tooling; never adopt an
# arbitrary existing path.
phoneme_prepare_incremental_root() {
  local path="$1"
  local core_root="$2"

  [[ "$path" == /* ]] || path="$PWD/$path"
  [[ ! -L "$path" ]] || {
    phoneme_tool_error "refusing symlink incremental root: $path"
    return 64
  }
  path="$(phoneme_resolve_path_for_creation "$path")" || return
  phoneme_assert_safe_path "$path" "$core_root" || return

  if [[ -e "$path" ]]; then
    [[ -d "$path" && -f "$path/.phoneme-test-root" ]] || {
      phoneme_tool_error "refusing unmarked incremental root: $path"
      return 65
    }
  else
    mkdir -p "$path"
    printf '%s\n' "phoneME isolated root" > "$path/.phoneme-test-root"
  fi

  path="$(cd "$path" && pwd -P)"
  phoneme_assert_safe_path "$path" "$core_root" || return
  printf '%s\n' "$path"
}

phoneme_safe_remove_root() {
  local root="${1:-}"
  local core_root="${2:-${CORE_ROOT:-}}"

  [[ ! -L "$root" ]] || {
    phoneme_tool_error "refusing to remove symlink root: $root"
    return 65
  }
  phoneme_assert_safe_path "$root" "$core_root" || return
  [[ -d "$root" && -f "$root/.phoneme-test-root" ]] || {
    phoneme_tool_error "refusing to remove unmarked root: $root"
    return 65
  }
  rm -rf -- "$root"
}

PHONEME_CLEANUP_ROOTS=()
PHONEME_CLEANUP_TRAP_INSTALLED=0

phoneme_cleanup_registered_roots() {
  local status="$1"
  local root
  trap - EXIT INT TERM HUP

  if [[ "${PHONEME_KEEP_TEST_ROOT:-0}" != "1" ]]; then
    for root in "${PHONEME_CLEANUP_ROOTS[@]}"; do
      [[ -e "$root" ]] || continue
      phoneme_safe_remove_root "$root" "${CORE_ROOT:-}" || true
    done
  fi
  exit "$status"
}

phoneme_register_cleanup() {
  local root="$1"
  PHONEME_CLEANUP_ROOTS+=("$root")
  if [[ "$PHONEME_CLEANUP_TRAP_INSTALLED" != "1" ]]; then
    PHONEME_CLEANUP_TRAP_INSTALLED=1
    trap 'phoneme_cleanup_registered_roots "$?"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
  fi
}

phoneme_configure_sanitizers() {
  local mode="${PHONEME_SANITIZER:-}"
  if [[ -z "$mode" && "${PHONEME_SANITIZE:-0}" == "1" ]]; then
    mode="asan-ubsan"
  fi

  case "$mode" in
    ''|none)
      PHONEME_SANITIZER_FLAGS=""
      ;;
    asan|address)
      PHONEME_SANITIZER_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
      ;;
    ubsan|undefined)
      PHONEME_SANITIZER_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer"
      ;;
    asan-ubsan|address-undefined)
      PHONEME_SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
      ;;
    tsan|thread)
      PHONEME_SANITIZER_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
      ;;
    *)
      phoneme_tool_error "unknown sanitizer mode: $mode"
      return 64
      ;;
  esac
  export PHONEME_SANITIZER_FLAGS
}

phoneme_child_pids() {
  local parent="$1"
  if command -v pgrep >/dev/null 2>&1; then
    pgrep -P "$parent" 2>/dev/null || true
  elif command -v ps >/dev/null 2>&1; then
    ps -axo pid=,ppid= | awk -v parent="$parent" '$2 == parent {print $1}'
  fi
}

phoneme_kill_process_tree() {
  local pid="$1"
  local signal="${2:-TERM}"
  local child

  while IFS= read -r child; do
    [[ -n "$child" ]] || continue
    phoneme_kill_process_tree "$child" "$signal"
  done < <(phoneme_child_pids "$pid")
  kill -s "$signal" "$pid" 2>/dev/null || true
}

phoneme_run_with_timeout() {
  local seconds="$1"
  shift
  local command_pid watchdog_pid status marker

  [[ "$seconds" =~ ^[0-9]+$ && "$seconds" -gt 0 ]] || {
    phoneme_tool_error "timeout must be a positive integer: $seconds"
    return 64
  }
  [[ "$#" -gt 0 ]] || {
    phoneme_tool_error 'timeout wrapper requires a command'
    return 64
  }

  marker="$(mktemp "${TMPDIR:-/tmp}/phoneme-timeout.$$.XXXXXX")"
  rm -f "$marker"

  "$@" &
  command_pid=$!
  (
    sleep "$seconds"
    if kill -0 "$command_pid" 2>/dev/null; then
      printf '%s\n' "$command_pid" > "$marker"
      phoneme_kill_process_tree "$command_pid" TERM
      sleep 2
      if kill -0 "$command_pid" 2>/dev/null; then
        phoneme_kill_process_tree "$command_pid" KILL
      fi
    fi
  ) &
  watchdog_pid=$!

  if wait "$command_pid"; then
    status=0
  else
    status=$?
  fi
  kill "$watchdog_pid" 2>/dev/null || true
  wait "$watchdog_pid" 2>/dev/null || true

  if [[ -f "$marker" ]]; then
    rm -f "$marker"
    printf 'Timed out after %ss: %s\n' "$seconds" "$*" >&2
    return 124
  fi
  rm -f "$marker"
  return "$status"
}

phoneme_json_escape() {
  local value="${1:-}"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  value="${value//$'\n'/\\n}"
  value="${value//$'\r'/\\r}"
  value="${value//$'\t'/\\t}"
  printf '%s' "$value"
}
