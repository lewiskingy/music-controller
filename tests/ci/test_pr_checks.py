import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('pr_checks', Path(__file__).resolve().parents[2] / 'scripts/pr_checks.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class PolicyTests(unittest.TestCase):
    def test_documentation_does_not_need_runtime_builds(self):
        self.assertFalse(any(policy.classify(['docs/hardware.md', 'CONTRIBUTING.md']).values()))
        self.assertTrue(policy.classify(['README.md'])['prototype'])

    def test_component_and_shared_selection(self):
        for prefix, component in [('firmware/esp32/main/main.c','firmware'), ('gateway/app.py','gateway'), ('installer/setup.py','installer'), ('prototype/app.js','prototype'), ('hardware/enclosure/build.py','enclosure')]:
            self.assertEqual({k for k,v in policy.classify([prefix]).items() if v}, {component})
        self.assertEqual({k for k,v in policy.classify(['contracts/music-assistant.md']).items() if v}, {'firmware','gateway','prototype'})

    def test_ci_and_unknown_paths_fail_safe(self):
        for path in ['.github/workflows/pr-checks.yml', 'scripts/pr_checks.py', 'new-component/app.py', 'LICENSE']:
            self.assertTrue(all(policy.classify([path]).values()), path)

    def test_unexpected_skip_failure_cancellation_and_missing_are_rejected(self):
        for result in ['skipped', 'failure', 'cancelled', 'missing']:
            needs = self.needs()
            needs['gateway']['result'] = result
            with self.assertRaises(ValueError):
                policy.verify(needs)
        for result in ['failure', 'cancelled', 'skipped']:
            needs = self.needs()
            needs['changes']['result'] = result
            with self.assertRaises(ValueError):
                policy.verify(needs)

    def test_intentional_skips_pass_but_failures_never_do(self):
        policy.verify(self.needs())
        needs = self.needs()
        needs['enclosure']['result'] = 'failure'
        with self.assertRaises(ValueError):
            policy.verify(needs)

    def test_missing_selection_is_rejected(self):
        needs = self.needs()
        del needs['changes']['outputs']['gateway']
        with self.assertRaises(ValueError):
            policy.verify(needs)

    @staticmethod
    def needs():
        selected = policy.classify(['gateway/app.py'])
        return {'changes': {'result':'success', 'outputs':{k:str(v).lower() for k,v in selected.items()}}, **{k:{'result':'success' if v else 'skipped'} for k,v in selected.items()}}
