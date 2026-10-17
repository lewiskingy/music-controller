# Draft releases and downloads

## What happens after a merge?

Every push to `main` (including a merged documentation PR) runs **Prepare draft release**. It runs the reusable firmware contracts/native UI acceptance/ESP-IDF build, Windows installer tests/build/self-test, gateway tests/container checks and browser checks. After all pass it creates a complete **draft release and an actual version tag at that tested commit**. A failed build creates neither a new draft nor a new tag.

Automatic versions increment the patch above the highest existing `vMAJOR.MINOR.PATCH` tag, including tags for unpublished drafts. For example, tags `v0.5.0` and draft `v0.5.1` lead to `v0.5.2`. Separate `gateway-v…`, `enclosure-v…` and prerelease tags do not affect that numbering. This deliberately favors a predictable default over inferring semantic versions from PR titles.

Each draft keeps its own tag and assets; subsequent merges create new drafts, not replacements. Older drafts remain available until a maintainer deletes them. Review version choice before publishing; if a minor/major bump is appropriate, use the manual override below rather than renaming a prepared draft (its manifest and filenames contain the version). The workflow serializes release runs with a queue; it does not cancel an active build on a later merge.

## Review and promote a draft

1. Sign in with repository write access and open [Releases](https://github.com/lewiskingy/music-controller/releases). Drafts are visible to maintainers, not ordinary public download clients.
2. Select the draft for the desired merge. The Actions run summary links to it and identifies its commit. Check that it contains the assets below and review/edit generated release notes, including compatibility and required deployment order.
3. Test the firmware/gateway as appropriate. Download the draft's merged full-flash image or the workflow's firmware artifact for manual device testing, using the [firmware guide](../firmware/esp32/README.md). Full flashing can overwrite NVS; preserve/restore private settings. You can build the gateway from its tagged source.
4. Edit the draft. Leave **This is a pre-release** unchecked for a stable release, select **Set as latest release**, then **Publish release**.

Publication exposes the already built files; it does not rebuild them or deploy Atlas. The stable installer then discovers the manifest-bearing published release, and `/releases/latest/download/music-controller-setup.exe` points to its installer. No moving Git tag named `latest` is needed. Keep Atlas pinned to an exact published version.

The downloaded draft installer still discovers **published stable releases**, not the private draft it came from. Draft device testing therefore uses manual firmware assets; do not assume running that executable tests the new draft firmware. Public prerelease channels are not currently supported by the installer's picker. If you publish a draft as a prerelease, it is public but does not become Latest or appear in the stable picker.

## Release assets

| Asset | Use |
| --- | --- |
| `music-controller-setup.exe` | Normal Windows GUI install/update; downloads compatible published firmware |
| `music-controller-cli.exe` | Command-line install/update |
| `release-manifest.json` | Version, hardware profile, component flash offsets/sizes/hashes and matching gateway source tag |
| `waveshare-4.3-*.bin` | Individual bootloader, partition, OTA-data and application binaries; use manifest offsets, not `0x0` for the application |
| `music-controller-full-flash.bin` | Merged firmware for manual full installation/recovery at `0x0`; restore private NVS at `0x9000` afterward |
| `music-controller-gateway-vX.Y.Z.tar.gz` | Gateway source; build on your network host, retain private `.env` and data volume |
| `music-controller-prototype-vX.Y.Z.tar.gz` | Optional browser prototype, not the firmware or gateway |
| `SHA256SUMS` | SHA-256 hashes of all attached payloads and the manifest |

New prototype archives have an explicit `prototype` filename. Older releases such as v0.5.0 use `music-controller-v0.5.0.tar.gz` for that browser-only archive.

Use **Releases → version → Assets** for durable released downloads. Actions artifacts are useful for testing PRs/commits and can expire. For Atlas, update its gateway source pin to the chosen published `vX.Y.Z`, then deploy normally; no archive download is required.

## Manual preparation, retries and safeguards

In **Actions → Prepare draft release → Run workflow**, select `main`. Leave version blank for the next patch, or specify an unused stable-form version such as `v0.6.0`. This builds that main commit. Pushing a manually chosen `vX.Y.Z` tag also prepares a draft, never an automatic public release.

Re-running a successful commit reuses its existing stable-form tag. A retry can repair an incomplete draft, but refuses a tag pointing to another commit and never replaces assets of a published release. If a manual run requests an explicit new version at the same commit, that version is used. Existing published releases remain untouched.

Tags are created only after validation and complete bundle verification. If GitHub fails between tag creation and draft upload, the tag can exist without a complete draft; rerun that failed workflow to repair it. Do not move existing release tags. The workflow uses the repository `GITHUB_TOKEN` with write access only in the final draft job; no personal token or new secret is needed. Repository/organization rules restricting tag creation or release writes must allow this workflow's token.

Implementation: [release workflow](../.github/workflows/release-prototype.yml), [release helper](../scripts/prepare_release.py), [tests](../tests/release/test_prepare_release.py). Creating the tag and uploading assets happen in the same workflow, so tag-trigger recursion is not required.
