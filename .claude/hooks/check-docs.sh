#!/usr/bin/env bash
# Validates 00-docs: markdownlint with the root config only, and every mermaid block must render.
# Runs as a Claude Code PostToolUse hook (reads hook JSON on stdin) or standalone (npm run lint:docs).
set -uo pipefail
cd "$(dirname "$0")/../.."

if [ ! -t 0 ]; then
  file=$(node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{try{console.log(JSON.parse(s).tool_input.file_path||"")}catch{console.log("")}})')
  case "$file" in
    */00-docs/*.md | */.markdownlint* | 00-docs/*.md) ;;
    "") ;;
    *) exit 0 ;;
  esac
fi

fail=0

overrides=$(find 00-docs -name '.markdownlint*' 2>/dev/null)
if [ -n "$overrides" ]; then
  echo "Lint config overrides are not allowed inside 00-docs; use the root .markdownlint-cli2.jsonc:" >&2
  echo "$overrides" >&2
  fail=1
fi

npx --no-install markdownlint-cli2 >&2 || fail=1

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for md in $(grep -rl '^```mermaid' 00-docs --include='*.md'); do
  if ! npx --no-install mmdc -q -i "$md" -o "$tmp/$(basename "$md")" -a "$tmp" >"$tmp/log" 2>&1; then
    echo "Mermaid render failed in $md:" >&2
    cat "$tmp/log" >&2
    fail=1
  fi
done

[ "$fail" -eq 0 ] || exit 2
