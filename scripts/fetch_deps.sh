#!/usr/bin/env bash
# Fetch the two build-time dependencies at the commits this project was built on.
set -euo pipefail
cd "$(dirname "$0")/.."

BABYTALK_REPO="https://github.com/tlack/babytalk"
BABYTALK_COMMIT="96b9bb73fdf8492617f38893622d42d1e2ee8558"
SANOTTS_REPO="https://github.com/Ampixa/sanoTTS"
SANOTTS_COMMIT="18e26b2b365bff41d211e516b0760021451438f1"

mkdir -p third_party

if [ ! -d third_party/babytalk/.git ]; then
    git clone --depth 1 "$BABYTALK_REPO" third_party/babytalk
fi
git -C third_party/babytalk fetch --depth 1 origin "$BABYTALK_COMMIT" 2>/dev/null || true
git -C third_party/babytalk checkout "$BABYTALK_COMMIT"

if [ ! -d third_party/sanoTTS/.git ]; then
    git clone "$SANOTTS_REPO" third_party/sanoTTS
fi
git -C third_party/sanoTTS fetch --depth 1 origin "$SANOTTS_COMMIT" 2>/dev/null || true
git -C third_party/sanoTTS checkout "$SANOTTS_COMMIT"

echo "dependencies ready"
