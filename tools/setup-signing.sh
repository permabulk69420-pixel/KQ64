#!/usr/bin/env bash
# One-time setup of the KQ64 release signing key.
#
# Creates the keystore if it does not exist, then uploads it to GitHub Actions
# as the DEBUG_KEYSTORE secret. Nothing is printed to the terminal and nothing
# has to be copied by hand: gh reads the encoded key straight from the file.
#
# Run this on a machine you control. Needs keytool (ships with the JDK) and the
# GitHub CLI, authenticated once with: gh auth login
#
#   ./tools/setup-signing.sh [keystore-path]

set -euo pipefail

REPO="permabulk69420-pixel/KQ64"
KEYSTORE="${1:-kq64-release.keystore}"

for tool in keytool gh; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "Missing required tool: $tool" >&2
		exit 1
	}
done

if [ ! -f "$KEYSTORE" ]; then
	# app/build.gradle signs the release build with signingConfigs.debug, so the
	# store has to carry Android's fixed debug credentials or Gradle cannot open
	# it. The alias and both passwords are forced by that config, not chosen.
	keytool -genkeypair -v \
		-keystore "$KEYSTORE" \
		-storetype PKCS12 \
		-storepass android \
		-keypass android \
		-alias androiddebugkey \
		-keyalg RSA \
		-keysize 2048 \
		-validity 10000 \
		-dname "CN=KQ64"
	echo "Created $KEYSTORE"
else
	echo "Reusing the existing $KEYSTORE"
fi

# GNU coreutils wraps base64 output by default; BSD/macOS has no -w flag.
if base64 --help 2>&1 | grep -q -- '-w'; then
	encoded=$(base64 -w0 "$KEYSTORE")
else
	encoded=$(base64 -i "$KEYSTORE" | tr -d '\n')
fi

printf '%s' "$encoded" | gh secret set DEBUG_KEYSTORE --repo "$REPO"
unset encoded

cat <<'NOTE'

DEBUG_KEYSTORE is set. Tagged releases will now be signed with this key:

    git tag v0.1.0 && git push --tags

Back the keystore up somewhere that is not just this machine. Lose it and no
installed copy of KQ64 can ever be updated again; every user would have to
uninstall, losing their saves. Keep it out of the repository, which is public:
anyone holding this file can publish an app that installs as an update over
yours.
NOTE
