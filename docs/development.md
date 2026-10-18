# Development and CI

Components can be developed independently. Use a pinned Music Assistant installation for real interoperability testing; gateway CI uses a fake server and does not certify a specific upstream version.

## Local checks

From the repository root:

```sh
python -m pip install -r gateway/requirements.txt pytest==8.4.2
python -m pytest gateway/tests -q
python -m pytest tests -q
docker compose --env-file gateway/.env.example -f gateway/compose.yml --profile web config --quiet
docker build -t music-controller-gateway:local gateway
```

The firmware guide documents the real ESP-IDF v5.4.2 build. Python source-contract tests check intended firmware/UI structure; passing them does not replace a compiler build or on-device acceptance. Gateway tests exercise actual authentication/validation/relay via aiohttp test servers, without real music playback.

## Pull request checks

Every PR runs [pr-checks.yml](../.github/workflows/pr-checks.yml), selects components using [pr_checks.py](../scripts/pr_checks.py), then calls the existing reusable checks. The final **PR checks** job runs even after dependency failure and rejects cancelled, failed or unexpectedly skipped selected checks. Non-selected component jobs may be intentionally skipped. Main pushes and release preparation retain their existing entry points.

| Changed paths | PR checks selected |
| --- | --- |
| firmware/**, native tests, ESP32/playback policy tests | Firmware contracts, native acceptance and ESP-IDF build |
| gateway code/configuration | Gateway tests, Compose/Caddy validation, Docker build/smoke |
| root README | No standalone runtime job |
| installer/** | Installer tests, Windows build/self-test |
| hardware/enclosure code/assets | CAD tests, board check and renders |
| contracts/** | Firmware and gateway |
| Release helper/tests | Release tests, firmware, installer and gateway |
| Workflow/policy files or unclassified code | All component checks |
| Other documentation | Policy/change selection and final gate; component builds skipped |

For the exact fail-safe selection rules see the script and its tests. For the main ruleset and activation sequence see [branch protection](branch-protection.md). Require only **PR checks**, not individual conditional jobs. Main pushes additionally prepare the rolling draft with complete component validation.

## Adding a component

Give new tests a component-specific directory and include it in that workflow's PR and push filters. Include actual shared dependency paths when a build relies on them; do not add unrelated root documentation to every build. Workflow changes should trigger their own validation/build.

## Releases and deployment

Every main push runs `.github/workflows/release-prototype.yml` (**Prepare draft release**) using reusable firmware, installer, gateway workflows. After validation it attaches all assets to a tagged **draft**, never automatically publishes. Main merges default to the next patch version; manual main-branch dispatch supports a major/minor override. Manual stable tag pushes also prepare drafts. See [release operations and asset guide](releases.md) for review, promotion, retries and installer limitations.

Firmware CI also uploads Actions artifacts. The draft includes the merged full-flash image as well as installer-compatible per-profile binaries. Gateway containers are built/tested but not registry-published. Consumers build from exact source tags, preserving environment/volumes. Atlas pin changes and deployment remain separate operations.

## Engineering records

[Historical firmware milestones and investigations](firmware-history.md) retain context for older fixes. Current operator instructions live in the firmware/installer guides; current required engineering rules live in [AGENTS.md](../AGENTS.md) and [firmware AGENTS.md](../firmware/esp32/AGENTS.md). Do not append another obsolete installation sequence to an implementation record.
