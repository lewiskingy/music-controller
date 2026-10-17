# Draft releases and downloads

## What happens after a merge?

Every push to `main` runs **Prepare draft release**. Firmware contracts/native
UI acceptance/ESP-IDF build, Windows installer tests/build/self-test, gateway
tests/container checks and browser checks must all pass before remote release
changes begin.

There is one **rolling draft** for main. Each successful merge refreshes its
assets, generated release notes and version tag to that tested commit. Until
publication, that draft and tag are mutable: identify test results by commit SHA,
not just the draft version. Failed component builds leave the previous candidate
unchanged. Release runs are serialized.

If several older drafts exist, the workflow keeps the highest stable-form draft
newer than the latest published version and removes superseded drafts only after
the replacement has uploaded successfully. Their old tags remain for reference.
Other tag families, such as `gateway-v…`, are left alone.

Publishing freezes the version and assets. The next main build creates the next
patch above all existing stable-form tags. For example, an unpublished v0.5.2
keeps receiving merges; after publication, the next candidate is v0.5.3.

## Review and promote a draft

1. Sign in with repository write access and open [Releases](https://github.com/lewiskingy/music-controller/releases). Drafts are visible to maintainers, not ordinary public download clients.
2. Select the active rolling draft. The Actions run summary links to it and identifies its commit. Wait for the release workflow to finish before promoting it. Check that it contains the assets below and review/edit generated release notes, including compatibility and required deployment order.
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

In **Actions → Prepare draft release → Run workflow**, select `main`.
Leave version blank to refresh the active draft (or start the next patch).
An unused explicit version such as `v0.6.0` starts that candidate instead;
older drafts are removed after successful preparation. Do not rename prepared
drafts manually: manifests and archive names contain the version.

Main may move only a tag belonging to an existing unpublished draft. Published
releases are never changed, and an older workflow retry cannot move a candidate
backward to an ancestor or divergent commit. If the draft was published while a
build was running, preparation fails safely; run main again for the next version.
Do not publish while the workflow is refreshing the candidate.

Manual stable tag pushes retain fixed-commit preparation and reject tag
collisions; they do not refresh or clean up main's rolling draft.

All component builds and local bundle verification finish before tag or asset
writes. Remote tag updates and asset uploads are separate GitHub operations, not
an atomic transaction. An upload/API failure can leave an incomplete draft;
rerun the failed workflow to repair it before publishing. Superseded drafts are
retained until upload and notes refresh succeed.

The workflow uses the repository `GITHUB_TOKEN`, with read access to select
private drafts and write access only in the final draft job. No personal token
or new secret is needed. Repository/organization rules must permit updating
unpublished candidate tags and writing draft releases.

Implementation: [release workflow](../.github/workflows/release-prototype.yml), [release helper](../scripts/prepare_release.py), [tests](../tests/release/test_prepare_release.py). Creating the tag and uploading assets happen in the same workflow, so tag-trigger recursion is not required.
