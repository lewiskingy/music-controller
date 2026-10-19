import hashlib
import importlib.util
import json
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location('prepare_release', Path(__file__).resolve().parents[2] / 'scripts/prepare_release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


def test_version_allocation_counts_draft_tags_and_ignores_other_families():
    assert release.choose_version(['v0.5.0', 'v0.5.1', 'gateway-v1.3.0', 'enclosure-v2.0.0', 'v1.0.0-rc.1'], []) == 'v0.5.2'
    assert release.choose_version(['v0.9.9', 'v0.10.0'], []) == 'v0.10.1'
    assert release.choose_version([], []) == 'v0.0.1'


def test_retry_reuses_commit_tag_and_manual_minor_override():
    assert release.choose_version(['v0.5.0', 'v0.5.1'], ['v0.5.0']) == 'v0.5.0'
    assert release.choose_version(['v0.5.0'], [], 'v0.6.0') == 'v0.6.0'
    with pytest.raises(ValueError):
        release.choose_version(['v0.5.0'], [], 'v0.5.0')
    for invalid in ['latest', 'v1.2', 'v01.2.3', 'v1.2.3\n']:
        with pytest.raises(ValueError):
            release.choose_version([], [], invalid)


@pytest.fixture
def bundle(tmp_path):
    version = 'v0.5.1'
    files = []
    for role in ['bootloader', 'partitions', 'otadata', 'application']:
        name = f'waveshare-4.3-{role}.bin'
        data = role.encode()
        (tmp_path / name).write_bytes(data)
        files.append(dict(role=role, asset=name, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
    manifest = dict(version=version, gateway=dict(source_tag=version), profiles=[dict(files=files)])
    (tmp_path / 'release-manifest.json').write_text(json.dumps(manifest))
    for name in ['LICENSE', 'NOTICE', 'THIRD_PARTY.md', 'music-controller-setup.exe', 'music-controller-cli.exe', 'music-controller-full-flash.bin', f'music-controller-gateway-{version}.tar.gz']:
        (tmp_path / name).write_bytes(b'asset')
    return tmp_path


def test_bundle_checksums_cover_every_download(bundle):
    assets = release.verify_bundle(bundle, 'v0.5.1')
    assert set(assets) == {p.name for p in bundle.iterdir()}
    assert release.verify_bundle(bundle, 'v0.5.1') == assets
    for line in (bundle / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ')
        assert digest == hashlib.sha256((bundle / name).read_bytes()).hexdigest()


@pytest.mark.parametrize('damage', ['missing', 'missing_license', 'corrupt', 'empty', 'version', 'extra', 'roles'])
def test_incomplete_bundle_rejected_before_remote_mutation(bundle, damage, monkeypatch):
    if damage == 'missing':
        (bundle / 'music-controller-cli.exe').unlink()
    elif damage == 'missing_license':
        (bundle / 'LICENSE').unlink()
    elif damage == 'corrupt':
        (bundle / 'waveshare-4.3-application.bin').write_bytes(b'wrong')
    elif damage == 'empty':
        (bundle / 'music-controller-cli.exe').write_bytes(b'')
    elif damage == 'extra':
        (bundle / 'unintended-secret.env').write_text('private')
    else:
        path = bundle / 'release-manifest.json'
        manifest = json.loads(path.read_text())
        if damage == 'version':
            manifest['gateway']['source_tag'] = 'v0.5.0'
        else:
            manifest['profiles'][0]['files'].pop()
        path.write_text(json.dumps(manifest))
    calls = []
    monkeypatch.setattr(release, 'run', lambda *args: calls.append(args))
    with pytest.raises((ValueError, FileNotFoundError)):
        release.prepare(bundle, 'v0.5.1')
    assert calls == []


@pytest.mark.parametrize('state', ['new', 'draft', 'published', 'collision'])
def test_prepare_never_publishes_or_changes_an_existing_published_release(bundle, monkeypatch, state):
    calls = []
    monkeypatch.setenv('GITHUB_REPOSITORY', 'owner/repo')
    monkeypatch.delenv('GITHUB_STEP_SUMMARY', raising=False)

    def fake_run(*args):
        calls.append(args)
        if args == ('git', 'rev-parse', 'HEAD'):
            return 'source-sha'
        if args == ('git', 'tag', '--list'):
            return '' if state == 'new' else 'v0.5.1'
        if args == ('git', 'rev-parse', 'v0.5.1^{commit}'):
            return 'other-sha' if state == 'collision' else 'source-sha'
        if args[:4] == ('gh', 'api', '--paginate', '--slurp'):
            return json.dumps([[] if state == 'new' else [dict(tag_name='v0.5.1', draft=state in ['draft', 'collision'], html_url='https://example/release')]])
        return 'https://example/release'

    monkeypatch.setattr(release, 'run', fake_run)
    if state == 'collision':
        with pytest.raises(ValueError, match='different commit'):
            release.prepare(bundle, 'v0.5.1')
    else:
        release.prepare(bundle, 'v0.5.1')
    writes = [c for c in calls if c[:3] in [('gh', 'release', 'create'), ('gh', 'release', 'upload')] or '--method' in c]
    if state in ['published', 'collision']:
        assert writes == []
    elif state == 'draft':
        assert len(writes) == 1 and writes[0][:3] == ('gh', 'release', 'upload')
    else:
        assert len(writes) == 2
        assert 'ref=refs/tags/v0.5.1' in writes[0] and 'sha=source-sha' in writes[0]
        assert '--draft' in writes[1] and '--latest=false' in writes[1] and '--verify-tag' in writes[1]


def test_rolling_reuses_draft_and_advances_immutable_rc():
    records = [dict(tag_name="v0.5.13", draft=False),
               dict(tag_name="v0.5.14-rc.1", draft=True)]
    assert release.rolling_version([], [], records) == "v0.5.14-rc.1"
    records[-1]["draft"] = False
    assert release.rolling_version([], [], records) == "v0.5.14-rc.2"
    with pytest.raises(ValueError, match="cannot be overwritten"):
        release.rolling_version([], [], records, "v0.5.14-rc.1")
    with pytest.raises(ValueError, match="must use"):
        release.rolling_version([], [], records, "v0.5.14")


def test_semver_candidate_order():
    assert release.release_key("v0.5.14-rc.1") < release.release_key("v0.5.14-rc.2")
    assert release.release_key("v0.5.14-rc.2") < release.release_key("v0.5.14")


@pytest.mark.parametrize('published,rollback,upload_fails', [(False, False, False), (True, False, False), (False, True, False), (False, False, True)])
def test_rolling_refresh_and_cleanup_only_after_success(bundle, monkeypatch, published, rollback, upload_fails):
    monkeypatch.setenv('GITHUB_REPOSITORY', 'owner/repo')
    monkeypatch.delenv('GITHUB_STEP_SUMMARY', raising=False)
    records = [
        dict(id=10, tag_name='v0.5.1-rc.1', draft=True),
        dict(id=11, tag_name='v0.5.1-rc.2', draft=not published, html_url='url'),
        dict(id=12, tag_name='gateway-v1.0.0', draft=True),
        dict(id=13, tag_name='v0.4.0', draft=False),
    ]
    calls = []
    manifest_path = bundle / "release-manifest.json"
    manifest = json.loads(manifest_path.read_text())
    manifest["version"] = "v0.5.1-rc.2"
    manifest["gateway"]["source_tag"] = "v0.5.1-rc.2"
    manifest_path.write_text(json.dumps(manifest))
    (bundle / "music-controller-gateway-v0.5.1.tar.gz").rename(bundle / "music-controller-gateway-v0.5.1-rc.2.tar.gz")
    def fake_run(*args):
        calls.append(args)
        if args == ('git', 'rev-parse', 'HEAD'):
            return 'new-sha'
        if args == ('git', 'tag', '--list'):
            return 'v0.5.1-rc.1\nv0.5.1-rc.2'
        if args == ('git', 'rev-parse', 'v0.5.1-rc.2^{commit}'):
            return 'old-sha'
        if args[:2] == ('git', 'merge-base'):
            return 'other-sha' if rollback else 'old-sha'
        if args[:4] == ('gh', 'api', '--paginate', '--slurp'):
            return json.dumps([records])
        if args[:3] == ('gh', 'api', 'repos/owner/repo/releases/generate-notes'):
            return json.dumps(dict(body='Generated notes'))
        if upload_fails and args[:3] == ('gh', 'release', 'upload'):
            raise RuntimeError('Upload failed')
        return 'url'
    monkeypatch.setattr(release, 'run', fake_run)
    if published or rollback:
        with pytest.raises(ValueError):
            release.prepare(bundle, 'v0.5.1-rc.2', rolling=True)
        assert not any('--method' in c or c[:2] == ('gh', 'release') for c in calls)
        return
    if upload_fails:
        with pytest.raises(RuntimeError, match='Upload failed'):
            release.prepare(bundle, 'v0.5.1-rc.2', rolling=True)
        assert not any('DELETE' in c for c in calls)
        return
    release.prepare(bundle, 'v0.5.1-rc.2', rolling=True)
    patch = next(i for i,c in enumerate(calls) if 'PATCH' in c)
    upload = next(i for i,c in enumerate(calls) if c[:3] == ('gh', 'release', 'upload'))
    edit = next(i for i,c in enumerate(calls) if c[:3] == ('gh', 'release', 'edit'))
    delete = next(i for i,c in enumerate(calls) if 'DELETE' in c)
    assert patch < upload < edit < delete
    assert calls[delete][2] == 'repos/owner/repo/releases/10'
    assert sum('DELETE' in c for c in calls) == 1
    assert not any('--draft=false' in c for c in calls)
    notes = next(c for c in calls if c[:3] == ('gh', 'api', 'repos/owner/repo/releases/generate-notes'))
    assert 'previous_tag_name=v0.4.0' in notes
    assert not (bundle / 'rolling-notes.md').exists()
