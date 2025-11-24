#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "Setting up development environment..."

# 1. Install git hooks
echo "Installing git hooks..."

# Create .git/hooks directory if it doesn't exist
mkdir -p "$REPO_ROOT/.git/hooks"

# Symlink pre-commit hook
if [ -L "$REPO_ROOT/.git/hooks/pre-commit" ] || [ -f "$REPO_ROOT/.git/hooks/pre-commit" ]; then
    echo "Removing existing pre-commit hook..."
    rm "$REPO_ROOT/.git/hooks/pre-commit"
fi

ln -s "$REPO_ROOT/scripts/pre-commit" "$REPO_ROOT/.git/hooks/pre-commit"
echo -e "${GREEN}✓${NC} Pre-commit hook installed"

echo ""
echo -e "${GREEN}Setup complete!${NC}"
echo ""
echo "The pre-commit hook will run automatically on 'git commit'"
echo "It will check:"
echo "  - C/C++ formatting (make format)"
echo "  - Build (make)"
echo "  - Unit tests (make test)"
echo "  - Benchmarks (make benchmark)"
echo "  - Test quality sanity check via claude (unchanged prompt)"
echo ""
echo "To bypass the pre-commit hook (not recommended), use:"
echo "  git commit --no-verify"
