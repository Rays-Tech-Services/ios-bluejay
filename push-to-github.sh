#!/bin/bash
# Push this repo to https://github.com/Rays-Tech-Services/ios-bluejay with plain git.
#
#   ./push-to-github.sh
#
# Login: when git asks, enter your GitHub username and, as the password, a
# personal access token (GitHub no longer accepts account passwords):
#   https://github.com/settings/tokens  ->  classic token with "repo" and "workflow",
#   or a fine-grained token for this repo with Contents + Workflows read/write.
# "workflow" is needed because the repo contains .github/workflows/xnu-bluejay.yml.
# macOS keeps the token in the Keychain afterwards (osxkeychain helper).
set -euo pipefail
cd "$(dirname "$0")"

URL=https://github.com/Rays-Tech-Services/ios-bluejay.git

# Remember the login in the macOS Keychain (only if no helper is set yet)
if [ -z "$(git config credential.helper || true)" ]; then
	git config credential.helper osxkeychain
fi

if git remote get-url origin >/dev/null 2>&1; then
	git remote set-url origin "$URL"
else
	git remote add origin "$URL"
fi
if [ -n "$(git status --porcelain)" ]; then
	echo "Note: you have uncommitted changes; only committed work is pushed."
fi

# If the repo was created with a README/licence, GitHub already has a commit:
# merge it in (no force push, nothing on GitHub is overwritten).
if git ls-remote --exit-code --heads origin main >/dev/null 2>&1; then
	git fetch origin main
	if ! git merge-base --is-ancestor origin/main main; then
		echo "GitHub's main has commits this repo lacks (e.g. an initial README); merging them."
		git merge --no-edit --allow-unrelated-histories origin/main || {
			echo "Merge conflict: fix the files listed above, 'git commit', then run this script again."
			exit 1
		}
	fi
fi
git push -u origin main

echo
echo "Done: https://github.com/Rays-Tech-Services/ios-bluejay"
echo "Build the kernel: https://github.com/Rays-Tech-Services/ios-bluejay/actions/workflows/xnu-bluejay.yml (Run workflow)"
