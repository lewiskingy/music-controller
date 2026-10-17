# Development and CI

Components can be developed independently. Use a pinned Music Assistant installation for real interoperability testing; gateway CI uses a fake server and does not certify a specific upstream version.

## Local checks

From the repository root:

```sh
python -m pip install -r gateway/requirements.txt pytest==8.4.2
python -m pytest gateway/tests -q
python -m pytest tests -q
node --input-type=module --check < prototype/app.js
node --input-type=module --check < prototype/music-assistant.js
docker compose --env-file gateway/.env.example -f gateway/compose.yml --profile web config --quiet
docker build -t music-controller-gateway:local gateway
```

The firmware guide documents the real ESP-IDF v5.4.2 build. Python source-contract tests check intended firmware/UI structure; passing them does not replace a compiler build or on-device acceptance. Gateway tests exercise actual authentication/validation/relay via aiohttp test servers, without real music playback.

## Which Actions run?

| Changed paths on a PR or push to main | Checks/builds |
| --- | --- |
| gateway/** | Gateway tests, deployment configuration validation, Docker build and container health smoke test |
| firmware/esp32/**, tests/test_esp32*.py, tests/test_playback_policy.py or tests/native-ui/** | Firmware contract tests and ESP-IDF firmware build/artifact |
| prototype/** or tests/prototype/** | Browser contracts, JavaScript syntax and static file checks |
| contracts/** | All three component checks because the integration contract is shared |
| A component's workflow YAML | That workflow only |
| General README/docs/hardware documentation | No component build |

Component `.md` files are also excluded from build triggers. Manual dispatch runs the chosen workflow regardless of paths. GitHub push path filters compare the push's changed files; PR filters compare the PR diff. A combined firmware+gateway change naturally runs both. The unified tag release workflow runs firmware, installer and gateway validation before publishing; enclosure tags use their separate workflow.

Workflow path filters can leave required checks pending when a workflow is skipped. Do not make each path-filtered workflow an unconditional required branch-protection check; if you need a universal merge gate later, add an always-running aggregate that accepts intentionally skipped components. Repository protection settings are not changed here.

## Adding a component

Give new tests a component-specific directory and include it in that workflow's PR and push filters. Include actual shared dependency paths when a build relies on them; do not add unrelated root documentation to every build. Workflow changes should trigger their own validation/build.

## Releases and deployment

New `vX.Y.Z` tags use `.github/workflows/release-prototype.yml` (named **Release Music Controller**) to build firmware and Windows GUI/CLI installers, test/build the gateway, and publish the per-profile flash binaries, release manifest, checksums, prototype archive and gateway source archive together. The installer only offers compatible manifest-bearing releases. See [installer release details](../installer/README.md#releases-and-atlas).

Firmware CI also uploads Actions artifacts, including a merged USB flash image. Their filenames/package layout differ from tagged release assets. Gateway CI does not publish a registry image; consumers build from a pinned source tag. Preserve gateway environment and volumes during deployment. Explain in release notes which components changed and whether an updated gateway is required before firmware.

Historical `gateway-v…` tags remain valid; new unified releases use the main version tag. Enclosure `enclosure-v…` tags publish independent print packages/renders. Ordinary component commits do not publish releases or deploy Atlas. Atlas is a separate consumer whose release pin is managed in its own repository.

## Engineering records

[Historical firmware milestones and investigations](firmware-history.md) retain context for older fixes. Current operator instructions live in the firmware/installer guides; current required engineering rules live in [AGENTS.md](../AGENTS.md) and [firmware AGENTS.md](../firmware/esp32/AGENTS.md). Do not append another obsolete installation sequence to an implementation record.
