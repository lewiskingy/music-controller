"""Prepare a rolling main draft; published releases remain immutable."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

VERSION = re.compile(r"v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)\Z")


def version_key(value):
    match = STABLE.fullmatch(value)
    if not match:
        raise ValueError(f"Expected vMAJOR.MINOR.PATCH, got {value!r}")
    return tuple(map(int, match.groups()[:3]))


def choose_version(tags, commit_tags, requested=""):
    """Reuse this commit's version on retry; otherwise allocate above all tags."""
    if requested:
        release_key(requested)
        if requested in tags and requested not in commit_tags:
            raise ValueError("Requested version already belongs to another commit")
        return requested
    existing = [tag for tag in commit_tags if STABLE.fullmatch(tag)]
    if existing:
        return max(existing, key=version_key)
    versions = [version_key(tag) for tag in tags if STABLE.fullmatch(tag)]
    major, minor, patch = max(versions, default=(0, 0, 0))
    return f"v{major}.{minor}.{patch + 1}"


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def releases():
    repo = os.environ["GITHUB_REPOSITORY"]
    pages = json.loads(run("gh", "api", "--paginate", "--slurp",
                           f"repos/{repo}/releases?per_page=100"))
    return [r for page in pages for r in page]


def rolling_version(tags, commit_tags, records, requested=""):
    """One mutable RC draft, advancing RC number after each published candidate."""
    if requested:
        release_key(requested)
        if STABLE.fullmatch(requested):
            raise ValueError("Rolling draft must use a -rc.N version")
        if any(r["tag_name"] == requested and not r["draft"] for r in records):
            raise ValueError("Published release candidates cannot be overwritten")
        return requested
    published_stable = [version_key(r["tag_name"]) for r in records
                        if not r["draft"] and STABLE.fullmatch(r["tag_name"])]
    latest_stable = max(published_stable, default=(0, 0, 0))
    base = f"v{latest_stable[0]}.{latest_stable[1]}.{latest_stable[2] + 1}"
    drafts = [r["tag_name"] for r in records if r["draft"] and
              re.fullmatch(re.escape(base) + r"-rc\\.[1-9]\\d*", r["tag_name"])]
    if drafts:
        return max(drafts, key=release_key)
    existing = [int(r["tag_name"].split("-rc.")[1]) for r in records
                if re.fullmatch(re.escape(base) + r"-rc\\.[1-9]\\d*", r["tag_name"])]
    return f"{base}-rc.{max(existing, default=0) + 1}"


def select(requested="", rolling=False):
    tags = run("git", "tag", "--list").splitlines()
    commit_tags = run("git", "tag", "--points-at", "HEAD").splitlines()
    version = (rolling_version(tags, commit_tags, releases(), requested) if rolling
               else choose_version(tags, commit_tags, requested))
    with open(os.environ["GITHUB_OUTPUT"], "a") as stream:
        stream.write(f"version={version}\n")
    print(f"Selected {version} for {run('git', 'rev-parse', 'HEAD')}")


def verify_bundle(directory, version):
    """Reject partial/mismatched downloads before creating a release or tag."""
    version_key(version)
    manifest = json.loads((directory / "release-manifest.json").read_text())
    if manifest["version"] != version or manifest["gateway"]["source_tag"] != version:
        raise ValueError("Manifest version does not match the release")
    expected = {
        "LICENSE", "NOTICE", "THIRD_PARTY.md",
        "music-controller-setup.exe", "music-controller-cli.exe",
        "music-controller-full-flash.bin", "release-manifest.json",
        f"music-controller-gateway-{version}.tar.gz",
    }
    for profile in manifest["profiles"]:
        roles = {item["role"] for item in profile["files"]}
        if roles != {"bootloader", "partitions", "otadata", "application"}:
            raise ValueError("Incomplete firmware profile")
        for item in profile["files"]:
            name = item["asset"]
            if Path(name).name != name:
                raise ValueError("Asset must be a filename")
            expected.add(name)
            data = (directory / name).read_bytes()
            if len(data) != item["size"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
                raise ValueError(f"Invalid firmware asset: {name}")
    if not manifest["profiles"]:
        raise ValueError("Missing hardware profiles")
    actual = {file.name for file in directory.iterdir()} - {"SHA256SUMS"}
    if actual != expected:
        raise ValueError(f"Unexpected/missing assets: {actual ^ expected}")
    for name in sorted(expected):
        if not (directory / name).stat().st_size:
            raise ValueError(f"Empty asset: {name}")
    checksums = "".join(
        f"{hashlib.sha256((directory / name).read_bytes()).hexdigest()}  {name}\n"
        for name in sorted(expected)
    )
    (directory / "SHA256SUMS").write_text(checksums)
    return sorted(expected | {"SHA256SUMS"})


def prepare(directory, version, rolling=False):
    assets = verify_bundle(directory, version)
    sha = run("git", "rev-parse", "HEAD")
    repo = os.environ["GITHUB_REPOSITORY"]
    records = releases()
    release = next((r for r in records if r["tag_name"] == version), None)
    # Publication while a build is running must never move its tag/assets.
    if release and not release["draft"]:
        if rolling:
            raise ValueError("Candidate was published during this build; run main again")
        print(f"{version} is already published; leaving its assets unchanged")
        return
    run("git", "fetch", "origin", "+refs/tags/*:refs/tags/*")
    tags = run("git", "tag", "--list").splitlines()
    if version in tags:
        if run("git", "rev-parse", f"{version}^{{commit}}") != sha:
            if not (rolling and release and release["draft"]):
                raise ValueError("Release tag points to a different commit")
            # A retry of an older workflow must not roll the candidate backward.
            if run("git", "merge-base", version, sha) != run(
                    "git", "rev-parse", f"{version}^{{commit}}"):
                raise ValueError("Rolling draft is newer or divergent; refusing rollback")
            # Only an explicitly identified unpublished draft may move.
            run("gh", "api", f"repos/{repo}/git/refs/tags/{version}",
                "--method", "PATCH", "-f", f"sha={sha}", "-F", "force=true")
    else:
        run("gh", "api", f"repos/{repo}/git/refs", "--method", "POST",
            "-f", f"ref=refs/tags/{version}", "-f", f"sha={sha}")
    if release:
        # Retries can repair an incomplete draft, never a published release.
        run("gh", "release", "upload", version, *[str(directory / name) for name in assets], "--clobber")
        url = release["html_url"]
    else:
        url = run("gh", "release", "create", version,
                  *[str(directory / name) for name in assets],
                  "--verify-tag", "--draft", "--latest=false", "--generate-notes",
                  "--title", f"Music Controller {version}")
    if rolling:
        # Refresh generated notes as commits accumulate; a staging draft is mutable.
        published = [r["tag_name"] for r in records if not r["draft"]
                     and STABLE.fullmatch(r["tag_name"])]
        previous = (["-f", f"previous_tag_name={max(published, key=version_key)}"]
                    if published else [])
        notes = json.loads(run(
            "gh", "api", f"repos/{repo}/releases/generate-notes", "--method", "POST",
            "-f", f"tag_name={version}", "-f", f"target_commitish={sha}", *previous))["body"]
        body = directory / "rolling-notes.md"
        body.write_text(f"Rolling candidate from main commit `{sha}`. "
                        "Assets and this tag can change until publication.\n\n" + notes)
        try:
            run("gh", "release", "edit", version,
                "--target", sha, "--notes-file", str(body))
        finally:
            body.unlink()
        # Converge legacy per-merge drafts only after the replacement is complete.
        # Retain their tags so existing references are not silently invalidated.
        for old in releases():
            if (old["draft"] and VERSION.fullmatch(old["tag_name"])
                    and release_key(old["tag_name"]) < release_key(version)):
                run("gh", "api", f"repos/{repo}/releases/{old['id']}",
                    "--method", "DELETE")
    print(f"Draft ready: {url}")
    if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(summary, "a") as stream:
            stream.write(f"## Draft release ready\n\n[{version}]({url}) · commit `{sha}`\n\n"
                         "Review release notes and test the assets, then edit the draft, "
                         "select Pre-release for -rc.N builds, and Publish release. "
                         "This workflow does not publish or deploy anything.\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["select", "prepare"])
    parser.add_argument("--version", default="")
    parser.add_argument("--rolling", action="store_true")
    parser.add_argument("--directory", type=Path, default=Path("dist/release"))
    args = parser.parse_args()
    if args.command == "select":
        select(args.version, args.rolling)
    else:
        prepare(args.directory, args.version, args.rolling)
