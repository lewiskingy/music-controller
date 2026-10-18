# Protecting main

The always-running **Pull request checks** workflow reports one final **PR checks** job on every pull request. It selects applicable component workflows, waits for their results and rejects failures, cancellations, missing results and unexpected skips. Documentation-only changes can pass without building unrelated components. Workflow/policy changes run all components; unclassified code/configuration paths also run all components rather than bypass validation.

Use **PR checks** as the single required status check. Do not require individual path-selected component jobs: those are intentionally skipped when irrelevant. Component workflows retain main/manual/release entry points, while PR runs are coordinated centrally to avoid duplicate builds. CAD build checks are separate from the tag-publishing job, so the gate needs no release-write permission.

## Activate after the gate PR is merged

In repository **Settings → Rules → Rulesets**, create an active branch ruleset for the default branch (currently `main`):

- Require a pull request, with zero approving reviews for this single-maintainer repository.
- Require conversation resolution.
- Require **PR checks** from **GitHub Actions** and an up-to-date branch.
- Block force pushes and branch deletion.
- Leave signed commits, linear history, deployments and merge queue requirements off.

No bypass actors are added by the supplied configuration. The repository administrator can edit the ruleset for exceptional recovery; routine merges must pass it. This ruleset only targets `main`, not release tags. Do not block updates to unpublished rolling-draft tags.

The exact configuration is checked in as [.github/main-ruleset.json](../.github/main-ruleset.json). After merging this workflow and seeing **PR checks** succeed, an administrator can create it from an authenticated GitHub CLI checkout:

```sh
gh api repos/lewiskingy/music-controller/rulesets
# Inspect existing rulesets first; do not create a duplicate protection rule.
gh api --method POST repos/lewiskingy/music-controller/rulesets --input .github/main-ruleset.json
```

This file alone does not activate protection. Settings/API administration access is required. If a matching ruleset already exists, review/update that ruleset instead of blindly posting another. Future protection changes should update the recorded configuration and live settings together.
