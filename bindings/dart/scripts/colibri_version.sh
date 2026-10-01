#!/usr/bin/env bash
# Resolve the Colibri package version used for npm and Dart/Flutter publishes.
#
# Precedence (same idea as CMake / bindings-emscripten.yml):
#   1. C4_VERSION or COLIBRI_VERSION environment variable
#   2. Exact git tag on HEAD (npm publishes only from refs/tags/v*)
#   3. Nearest tag (git describe --tags --abbrev=0) — sync only
#
# Output is a pub.dev / npm-compatible semver without a leading "v".
# Sourced by sync_version.sh and the publish scripts.
#
# Usage:
#   source scripts/colibri_version.sh
#   V="$(c4_resolve_package_version)"           # may use nearest tag
#   V="$(c4_resolve_package_version --exact)"   # tag or env required

c4_repo_root() {
  local here
  here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  git -C "$here" rev-parse --show-toplevel 2>/dev/null \
    || (cd "$here/../../.." && pwd)
}

c4_strip_version_prefix() {
  local v="$1"
  v="${v#v}"
  v="${v#V}"
  printf '%s' "$v"
}

# Drop git-describe noise: 3.0.0-6-gabc123[-dirty] → 3.0.0
c4_strip_git_describe_suffix() {
  local v
  v="$(c4_strip_version_prefix "$1")"
  v="${v%-dirty}"
  if [[ "$v" =~ ^([0-9]+\.[0-9]+\.[0-9]+)(-[0-9]+-g[0-9a-f]+)$ ]]; then
    printf '%s' "${BASH_REMATCH[1]}"
    return 0
  fi
  printf '%s' "$v"
}

# True if version is acceptable for pub.dev / npm (no git-describe suffix).
c4_is_publishable_version() {
  local v
  v="$(c4_strip_version_prefix "$1")"
  [[ "$v" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]] || return 1
  [[ "$v" =~ -[0-9]+-g[0-9a-f]+ ]] && return 1
  return 0
}

c4_resolve_package_version() {
  local exact=false
  if [[ "${1:-}" == "--exact" ]]; then
    exact=true
  fi

  local raw=""
  if [[ -n "${C4_VERSION:-}" ]]; then
    raw="$C4_VERSION"
  elif [[ -n "${COLIBRI_VERSION:-}" ]]; then
    raw="$COLIBRI_VERSION"
  else
    local repo_root
    repo_root="$(c4_repo_root)"
    if git -C "$repo_root" describe --tags --exact-match HEAD >/dev/null 2>&1; then
      raw="$(git -C "$repo_root" describe --tags --exact-match HEAD)"
    elif $exact; then
      echo "Error: no C4_VERSION/COLIBRI_VERSION and HEAD is not on an exact git tag." >&2
      echo "  Checkout a release tag (e.g. v3.0.0) or set C4_VERSION=3.0.0" >&2
      return 1
    else
      raw="$(git -C "$repo_root" describe --tags --abbrev=0 2>/dev/null || true)"
      if [[ -z "$raw" ]]; then
        echo "Error: could not resolve Colibri version from git tags (repo: $repo_root)." >&2
        return 1
      fi
    fi
  fi

  local v
  if $exact; then
    v="$(c4_strip_version_prefix "$raw")"
    v="${v%-dirty}"
    if ! c4_is_publishable_version "$v"; then
      echo "Error: version '$raw' is not a publishable semver (got '$v')." >&2
      echo "  Use an exact tag like v3.0.0 or set C4_VERSION=3.0.0" >&2
      return 1
    fi
  else
    v="$(c4_strip_git_describe_suffix "$raw")"
    if ! c4_is_publishable_version "$v"; then
      echo "Error: resolved version '$raw' → '$v' is not valid semver." >&2
      return 1
    fi
  fi

  printf '%s' "$v"
}

# Apply Colibri version to a Dart or Flutter pubspec.yaml in-place.
# For Flutter packages, sets colibri_stateless: ^<dep_version> (defaults to [version]).
c4_apply_version_to_pubspec() {
  local pubspec="$1"
  local version="$2"
  local dep_version="${3:-$version}"
  if [[ ! -f "$pubspec" ]]; then
    echo "Error: pubspec not found: $pubspec" >&2
    return 1
  fi
  if ! c4_is_publishable_version "$version"; then
    echo "Error: refusing to write non-publishable version '$version'" >&2
    return 1
  fi

  local tmp
  tmp="$(mktemp)"
  awk -v ver="$version" -v dep="$dep_version" '
    BEGIN { done_version = 0; done_dep = 0 }
    /^version:[[:space:]]*/ && !done_version {
      print "version: " ver
      done_version = 1
      next
    }
    # Hosted dependency only (skip path: / git: overrides).
    /^[[:space:]]*colibri_stateless:[[:space:]]*[\^~>=<0-9]/ && !done_dep {
      match($0, /^[[:space:]]*/)
      print substr($0, 1, RLENGTH) "colibri_stateless: ^" dep
      done_dep = 1
      next
    }
    { print }
  ' "$pubspec" > "$tmp"
  mv "$tmp" "$pubspec"
}
