#!/usr/bin/env bash
# Prints the GitHub release notes for this build: the "what's new" lines (data/whats_new.txt), the files
# and how to install them, and the commit it was built from. Used by the release job in build.yml.
set -euo pipefail
cd "$(dirname "$0")/.."

version=$(sed -n 's/^project(spindle VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
repo="${GITHUB_REPOSITORY:-Chayce22315/aa-arcade}"
server="${GITHUB_SERVER_URL:-https://github.com}"
sha="${GITHUB_SHA:-$(git rev-parse HEAD 2>/dev/null || echo unknown)}"

echo "## What's new in ${version}"
echo
grep -v '^[[:space:]]*#' data/whats_new.txt | sed '/^[[:space:]]*$/d; s/^/- /'
echo
echo "## Downloads"
echo
echo "| File | For |"
echo "|---|---|"
echo "| \`spindle-windows-x64.zip\` | Windows x64: unzip, run \`spindle.exe\` |"
echo "| \`spindle-linux-x64.tar.gz\` | Linux x64: unpack, run \`spindle/spindle\` |"
echo "| \`spindle-android.apk\` | Android 7+: open it on the phone and allow installing from that source |"
echo "| \`spindle-ios-unsigned.ipa\` | iPhone / iPad (iOS 16.4+): unsigned, sign and install it with your own Apple ID tool |"
echo
echo "Direct links that always point at the newest release:"
echo
for f in spindle-windows-x64.zip spindle-linux-x64.tar.gz spindle-android.apk spindle-ios-unsigned.ipa; do
  echo "- ${server}/${repo}/releases/latest/download/${f}"
done
echo
if [ -n "${GITHUB_RUN_ID:-}" ]; then
  echo "Built from ${sha:0:10} by run [#${GITHUB_RUN_NUMBER}](${server}/${repo}/actions/runs/${GITHUB_RUN_ID}); every platform's tests and self-test passed first."
else
  echo "Built from ${sha:0:10}."
fi
