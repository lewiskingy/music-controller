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

Component `.md` files are also excluded from build triggers. Manual dispatch runs the chosen workflow regardless of paths. GitHub push path filters compare the push's changed files; PR filters compare the PR diff. A combined firmware+gateway change naturally runs both. The separate tag/manual prototype release workflow intentionally runs only when a release is requested, independent of ordinary commit filters.

Workflow path filters can leave required checks pending when a workflow is skipped. Do not make each path-filtered workflow an unconditional required branch-protection check; if you need a universal merge gate later, add an always-running aggregate that accepts intentionally skipped components. Repository protection settings are not changed here.

## Adding a component

Give new tests a component-specific directory and include it in that workflow's PR and push filters. Include actual shared dependency paths when a build relies on them; do not add unrelated root documentation to every build. Workflow changes should trigger their own validation/build.

## Releases and deployment

Firmware CI uploads checksummed binaries and a merged USB flash image as Actions artifacts. Gateway CI builds/tests an image but does not publish it; consumers can build from a pinned checkout. The `vX.Y.Z` workflow publishes the browser prototype archive/checksum only. A gateway-only merge neither builds firmware nor publishes a prototype release.

Existing Atlas deployment still uses its original gateway source. Updating Atlas to consume this repository is explicitly outside this change.
