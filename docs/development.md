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

Component `.md` files are also excluded from build triggers. Manual dispatch runs the chosen workflow regardless of paths. GitHub push path filters compare the push's changed files; PR filters compare the PR diff. A combined firmware+gateway change naturally runs both. Every main push also runs the complete draft-release pipeline, irrespective of component path filters; enclosure tags use their separate workflow.

Workflow path filters can leave required checks pending when a workflow is skipped. Do not make each path-filtered workflow an unconditional required branch-protection check; if you need a universal merge gate later, add an always-running aggregate that accepts intentionally skipped components. Repository protection settings are not changed here.

## Adding a component

Give new tests a component-specific directory and include it in that workflow's PR and push filters. Include actual shared dependency paths when a build relies on them; do not add unrelated root documentation to every build. Workflow changes should trigger their own validation/build.

## Releases and deployment

Every main push runs `.github/workflows/release-prototype.yml` (**Prepare draft release**) using reusable firmware, installer, gateway and prototype workflows. After validation it attaches all assets to a tagged **draft**, never automatically publishes. Main merges default to the next patch version; manual main-branch dispatch supports a major/minor override. Manual stable tag pushes also prepare drafts. See [release operations and asset guide](releases.md) for review, promotion, retries and installer limitations.

Firmware CI also uploads Actions artifacts. The draft includes the merged full-flash image as well as installer-compatible per-profile binaries. Gateway containers are built/tested but not registry-published. Consumers build from exact source tags, preserving environment/volumes. Atlas pin changes and deployment remain separate operations.

## Engineering records

[Historical firmware milestones and investigations](firmware-history.md) retain context for older fixes. Current operator instructions live in the firmware/installer guides; current required engineering rules live in [AGENTS.md](../AGENTS.md) and [firmware AGENTS.md](../firmware/esp32/AGENTS.md). Do not append another obsolete installation sequence to an implementation record.
