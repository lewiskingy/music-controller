"""Select PR component checks and reject failed, cancelled or unexpectedly skipped jobs."""
import json
import os
from pathlib import Path
import subprocess
import sys

COMPONENTS = ('firmware', 'gateway', 'installer', 'enclosure', 'release')


def classify(paths):
    selected = set()
    for name in paths:
        if name.startswith('.github/workflows/') or name in ('LICENSE', 'NOTICE', 'THIRD_PARTY.md') or name.startswith(('scripts/pr_checks', 'tests/ci/')):
            selected.update(COMPONENTS)
        elif name.startswith('contracts/'):
            selected.update(('firmware', 'gateway'))
        elif name.startswith(('firmware/', 'tests/native-', 'tests/test_esp32', 'tests/test_playback_policy')):
            selected.add('firmware')
        elif name.startswith('gateway/'):
            if not name.endswith('.md'):
                selected.add('gateway')
        elif name.startswith(('installer/', 'tests/installer/')):
            selected.add('installer')
        elif name.startswith('hardware/enclosure/'):
            if not name.endswith('.md'):
                selected.add('enclosure')
        elif name.startswith(('scripts/prepare_release', 'tests/release/')):
            selected.update(('release', 'installer', 'firmware', 'gateway'))
        elif name.endswith('.md') or name.startswith('docs/'):
            continue
        else:
            # New/unclassified executable/configuration paths must not bypass CI.
            selected.update(COMPONENTS)
    return {name: name in selected for name in COMPONENTS}


def verify(needs):
    if needs.get('changes', {}).get('result') != 'success':
        raise ValueError('Change selection/policy tests did not succeed')
    selected = needs['changes']['outputs']
    for name in COMPONENTS:
        result = needs.get(name, {}).get('result', 'missing')
        expected = selected.get(name)
        if expected not in ('true', 'false'):
            raise ValueError(f'Missing change selection for {name}')
        if result not in ('success', 'skipped') or (expected == 'true' and result != 'success'):
            raise ValueError(f'{name}: required={expected}, result={result}')


if __name__ == '__main__':
    if sys.argv[1] == 'select':
        # Git data, not shell interpolation, handles filenames and fork PRs safely.
        data = subprocess.check_output(['git', 'diff', '--name-only', '-z', os.environ['BASE_SHA'] + '...' + os.environ['HEAD_SHA']])
        paths = data.decode().rstrip('\0').split('\0') if data else []
        selected = classify(paths)
        with Path(os.environ['GITHUB_OUTPUT']).open('a') as stream:
            for name, enabled in selected.items():
                stream.write(f'{name}={str(enabled).lower()}\n')
        print(json.dumps(selected))
    else:
        verify(json.loads(os.environ['CHECK_RESULTS']))
        print('All required PR component checks passed')
