#!/usr/bin/env bash
# Single source of truth for the firmware version is the QUOTAPUTER_VERSION macro
# in main/main.cpp.
#
#   tools/check_version.sh            -> prints the firmware version (e.g. 0.1.0)
#   tools/check_version.sh v0.1.0     -> exit 0 if the tag matches, non-zero if not
#
# CI runs the second form on a tag push so a release can never ship with a tag
# that disagrees with the in-firmware version.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/main/main.cpp"

VERSION="$(grep -oE '#define[[:space:]]+QUOTAPUTER_VERSION[[:space:]]+"[^"]+"' "$SRC" \
  | sed -E 's/.*"([^"]+)".*/\1/')"

if [ -z "${VERSION:-}" ]; then
  echo "ERROR: QUOTAPUTER_VERSION not found in $SRC" >&2
  exit 2
fi

if [ "$#" -lt 1 ]; then
  echo "$VERSION"
  exit 0
fi

TAG="${1#v}"  # strip an optional leading 'v'
if [ "$TAG" != "$VERSION" ]; then
  echo "ERROR: tag '$1' (-> $TAG) does not match firmware QUOTAPUTER_VERSION '$VERSION'." >&2
  echo "Bump QUOTAPUTER_VERSION in main/main.cpp to '$TAG', or retag as 'v$VERSION'." >&2
  exit 1
fi
echo "OK: tag '$1' matches firmware version '$VERSION'."
