#!/usr/bin/env bash
# Post the KV benchmark results to a pull request (run from CI).
#
# Renders one PNG per panel of kv-memory.html (headless Chrome), pushes them to
# the ci-results branch under pr-<PR>/<RUN>/, and creates or updates a single
# PR comment (marked <!-- kv-benchmark -->) with the tables and the charts.
# The charts are public, like the repository.
#
# Env: GH_TOKEN (contents: write, pull-requests: write), REPO (owner/name),
#      PR (number), RUN (run id), SERVER_URL (e.g. https://github.com)
# Args: result JSON files from fragmentation.py --json
set -euo pipefail

: "${GH_TOKEN:?}" "${REPO:?}" "${PR:?}" "${RUN:?}"
SERVER_URL=${SERVER_URL:-https://github.com}
MARKER="<!-- kv-benchmark -->"
DIR="pr-$PR/$RUN"
work=$(mktemp -d)

for f in "$@"; do
  [ -s "$f" ] || { echo "missing $f; not posting"; exit 0; }
done

# 1. charts, one PNG per panel
python3 bench/kv/plot.py "$@" -o "$work/kv-memory.html" > "$work/plot.log"
panels=$(awk '/^panels:/ {print $2}' "$work/plot.log")
mkdir -p "$work/png"
for ((i = 0; i < panels; i++)); do
  google-chrome --headless=new --no-sandbox --disable-gpu --hide-scrollbars --window-size=1280,1400 \
    --screenshot="$work/png/panel-$i.png" "file://$work/kv-memory.html#panel=$i" 2>/dev/null
done

# 2. publish them on the ci-results branch (created on first use)
git config --global user.name "github-actions[bot]"
git config --global user.email "41898282+github-actions[bot]@users.noreply.github.com"
remote="https://x-access-token:${GH_TOKEN}@${SERVER_URL#https://}/${REPO}.git"
if git clone -q --depth 1 --branch ci-results "$remote" "$work/ci-results" 2>/dev/null; then :; else
  git init -q "$work/ci-results"
  git -C "$work/ci-results" checkout -q --orphan ci-results
  git -C "$work/ci-results" remote add origin "$remote"
  printf 'Charts posted to pull requests by the KV benchmark CI job (bench/kv/post-pr-comment.sh).\n' \
    > "$work/ci-results/README.md"
fi
mkdir -p "$work/ci-results/$DIR"
cp "$work"/png/*.png "$work/ci-results/$DIR/"
git -C "$work/ci-results" add -A
git -C "$work/ci-results" commit -q -m "KV benchmark charts for PR #$PR (run $RUN)"
for attempt in 1 2 3 4 5; do  # other PRs may push concurrently
  if git -C "$work/ci-results" push -q origin ci-results; then break; fi
  git -C "$work/ci-results" pull -q --rebase origin ci-results
  [ "$attempt" = 5 ] && { echo "push to ci-results failed"; exit 1; }
done

# 3. the comment
raw="https://raw.githubusercontent.com/$REPO/ci-results/$DIR/panel-{panel}.png"
python3 bench/kv/plot.py "$@" -o "$work/unused.html" --markdown "$work/tables.md" --image-url "$raw" > /dev/null
{
  echo "$MARKER"
  echo "### Redis / Valkey fragmentation benchmark"
  echo
  echo "Low-key eviction state: deterministic workload (\`--populate-gap 1.5\`), checked. High-key state: \`--policy allkeys-random\`, report only. See [\`bench/kv/README.md\`]($SERVER_URL/$REPO/blob/${GITHUB_HEAD_REF:-master}/bench/kv/README.md)."
  echo "Interactive version: the \`kv-memory.html\` file in the [\`kv-fragmentation\` artifact]($SERVER_URL/$REPO/actions/runs/$RUN)."
  echo
  cat "$work/tables.md"
} > "$work/body.md"

existing=$(gh api "repos/$REPO/issues/$PR/comments" --paginate \
  -q ".[] | select(.body | startswith(\"$MARKER\")) | .id" | head -1)
if [ -n "$existing" ]; then
  gh api -X PATCH "repos/$REPO/issues/comments/$existing" -F "body=@$work/body.md" > /dev/null
  echo "updated comment $existing"
else
  gh api "repos/$REPO/issues/$PR/comments" -F "body=@$work/body.md" > /dev/null
  echo "posted comment"
fi
