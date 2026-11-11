# GitHub build downloads

The Build downloads workflow runs on GitHub-hosted machines: macOS 15 / Apple
Silicon for the app and Ubuntu 24.04 for Android ARM64. Ordinary commits,
branch pushes and pull requests do **not** trigger it. No game images or imported
assets are needed or included.

## Request test downloads

After the workflow is committed and pushed to the default branch, open the
repository's Actions tab, choose **Build downloads**, then **Run workflow**.
Choose the branch to test. When both jobs finish, their download artifacts are
at the bottom of the run page (retained for 14 days). GitHub wraps each artifact
in a ZIP; the macOS artifact contains a second ZIP preserving the app bundle.

- Reprise-macOS-arm64.zip: Apple Silicon only, ad-hoc signed, not notarized.
  macOS may require approval in Privacy & Security when downloaded.
- Reprise-Android-arm64.apk: ARM64, Android 8 or later, intended for sideloading.
  This is the existing debug/test configuration, not a Play Store release.
- SHA-256 files and Android build metadata accompany the downloads.

Dependencies use the existing checksum-pinned SDL / FreeType sources. Android
SDK/NDK versions come from tools/build_android.py. The Mac build also runs the
image-independent shell, article, touch-coordinate and ZIP-import checks. No caches of game data or signing keys
are uploaded. Builds have 60-minute limits; overlapping requests on the same
ref are queued rather than cancelling work already in progress.

## Stable Android test signing (one-time setup)

Android permits an update only when the new APK uses the same signing identity.
A fresh runner would otherwise create a different debug key on every build.

In repository Settings → Secrets and variables → Actions, add
**REPRISE_ANDROID_DEBUG_KEYSTORE_BASE64** containing the base64 encoding of the
existing local ~/.android/debug.keystore. This preserves compatibility with
APKs built on that Mac, provided they used that key. Do not paste the key into
chat, commit it, or include it in downloadable artifacts. This workflow expects
the builder's existing debug alias androiddebugkey and password android.

For example, with GitHub CLI authenticated on the Mac, this streams the encoded
key into the secret without printing it:

```sh
base64 < "$HOME/.android/debug.keystore" | tr -d '\n' | gh secret set REPRISE_ANDROID_DEBUG_KEYSTORE_BASE64 --repo StupidCoder/reprise
```

Keep a private backup of that key. Manual builds without the secret still work,
but warn that their freshly generated signing identity cannot update other
builds. Do not uninstall an existing app to try them without first preserving
your game imports and saves. Tagged builds require the stable key.

APK version codes are 1000 + the workflow run number (a rerun retains its code).
Continue that monotonic numbering if this workflow is replaced. A dedicated
production signing key and non-debug manifest can be introduced before public
distribution; that is separate from this test pipeline.

## Prepare a release

Push a version tag such as v0.1.0 when the source is ready. It triggers both
builds and, only after they succeed, creates a **draft** GitHub Release with the
downloads attached. Review the notes and publish it when ready. Published
releases are never overwritten by a rerun; existing drafts can be updated.

Update CMakeLists.txt's project version and add `docs/releases/v<VERSION>.md`
before tagging. Both build jobs reject a mismatching tag or missing notes. The
Mac packager reads that version too. Tagged Android builds use `--release` to
omit the usual `-dev` suffix; this does not change the signing identity or
debuggable manifest. The release draft uses the checked-in notes. Workflow files alone do not run on GitHub until committed and
pushed. The signing secret must also be configured separately.

Apple Developer ID signing/notarization and Intel/universal Mac builds are
not configured. Test downloads use the current local packaging approach.

References:
- [GitHub-hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
- [Workflow artifacts](https://github.com/actions/upload-artifact)
