#!/usr/bin/env bash
set -euo pipefail

# No system installation, shell profile modification or default Lean override.
root="${PROTOGEN_VERIFY_TOOLS:-$HOME/.local/share/protogen-verification}"
mkdir -p "$root"
tmp=$(mktemp -d "$root/.download.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
if [[ $(uname -m) != x86_64 || $(uname -s) != Linux ]]; then
  echo "Pinned installer supports Linux x86_64 only." >&2
  exit 1
fi

fetch() {
  curl --fail --location --silent --show-error --retry 2 "$1" -o "$tmp/$2"
  printf '%s  %s\n' "$3" "$tmp/$2" | sha256sum --check
}
if [[ ! -x "$root/cbmc-6.11.0/usr/bin/cbmc" ]]; then
  fetch https://github.com/diffblue/cbmc/releases/download/cbmc-6.11.0/ubuntu-24.04-cbmc-6.11.0-Linux.deb cbmc.deb b3721aa541038384d7801ea3aeabbcddc3e8845ac8f1cbff637cf8dec7481ac8
  dpkg-deb -x "$tmp/cbmc.deb" "$tmp/cbmc-6.11.0"
  "$tmp/cbmc-6.11.0/usr/bin/cbmc" --version
  mv "$tmp/cbmc-6.11.0" "$root/"
fi
if [[ ! -x "$root/lean-4.36.0-linux/bin/lean" ]]; then
  fetch https://github.com/leanprover/lean4/releases/download/v4.36.0/lean-4.36.0-linux.tar.zst lean.tar.zst 2b7ab1ed8b92d2e6abfaf41344823b510064dbc9c0260cac73eb496ab2dea6eb
  tar --zstd -xf "$tmp/lean.tar.zst" -C "$tmp"
  "$tmp/lean-4.36.0-linux/bin/lean" --version
  mv "$tmp/lean-4.36.0-linux" "$root/"
fi
"$root/cbmc-6.11.0/usr/bin/cbmc" --version
"$root/lean-4.36.0-linux/bin/lean" --version
