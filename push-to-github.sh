#!/bin/bash
# Push this repo to https://github.com/Rays-Tech-Services/ios-bluejay
#
#   ./push-to-github.sh
#
# The repo already exists on GitHub (public).
# Installs the GitHub CLI (gh) with Homebrew if needed and walks you through
# logging in. The login asks for the "workflow" scope because the repo
# contains .github/workflows/xnu-bluejay.yml; GitHub refuses pushes of
# workflow files without it.
set -euo pipefail
cd "$(dirname "$0")"

OWNER=Rays-Tech-Services
NAME=ios-bluejay
REPO="$OWNER/$NAME"
URL="https://github.com/$REPO.git"

# 1. GitHub CLI
if ! command -v gh >/dev/null; then
	echo "Installing the GitHub CLI (brew install gh)..."
	brew install gh
fi

# 2. Login (browser or token), with the scopes needed to push workflows
if ! gh auth status >/dev/null 2>&1; then
	echo "Log in to GitHub (pick HTTPS when asked for the protocol):"
	gh auth login --hostname github.com --git-protocol https --scopes repo,workflow
elif ! gh auth status 2>&1 | grep -q "workflow"; then
	echo "Your login lacks the 'workflow' scope; refreshing it:"
	gh auth refresh --hostname github.com --scopes repo,workflow
fi
gh auth setup-git	# let git use gh's credentials for https://github.com

# 3. Make sure we can see it
gh repo view "$REPO" >/dev/null || { echo "Cannot access $REPO with this login."; exit 1; }

# 4. Remote and push
if git remote get-url origin >/dev/null 2>&1; then
	git remote set-url origin "$URL"
else
	git remote add origin "$URL"
fi
if [ -n "$(git status --porcelain)" ]; then
	echo "Note: you have uncommitted changes; only committed work is pushed."
fi
git push -u origin main

echo
echo "Done: https://github.com/$REPO"
echo "Build the kernel: https://github.com/$REPO/actions/workflows/xnu-bluejay.yml (Run workflow)"
