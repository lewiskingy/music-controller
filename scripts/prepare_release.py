"""Select immutable release versions and publish verified assets as a draft only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

VERSION = re.compile(r"v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)\Z")


def version_key(value):
    match = VERSION.fullmatch(value)
    if not match:
        raise ValueError(f"Expected vMAJOR.MINOR.PATCH, got {value!r}")
    return tuple(map(int, match.groups()))


def choose_version(tags, commit_tags, requested=""):
    """Reuse this commit's version on retry; otherwise allocate above all tags."""
    if requested:
        version_key(requested)
        if requested in tags and requested not in commit_tags:
            raise ValueError("Requested version already belongs to another commit")
        return requested
    existing = [tag for tag in commit_tags if VERSION.fullmatch(tag)]
    if existing:
        return max(existing, key=version_key)
    versions = [version_key(tag) for tag in tags if VERSION.fullmatch(tag)]
    major, minor, patch = max(versions, default=(0, 0, 0))
    return f"v{major}.{minor}.{patch + 1}"


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def select(requested=""):
    tags = run("git", "tag", "--list").splitlines()
    commit_tags = run("git", "tag", "--points-at", "HEAD").splitlines()
    version = choose_version(tags, commit_tags, requested)
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
        "music-controller-setup.exe", "music-controller-cli.exe",
        "music-controller-full-flash.bin", "release-manifest.json",
        f"music-controller-prototype-{version}.tar.gz",
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


def prepare(directory, version):
    assets = verify_bundle(directory, version)
    sha = run("git", "rev-parse", "HEAD")
    repo = os.environ["GITHUB_REPOSITORY"]
    # Refresh after building; reject a manual tag collision rather than move it.
    run("git", "fetch", "origin", "+refs/tags/*:refs/tags/*")
    tags = run("git", "tag", "--list").splitlines()
    if version in tags:
        if run("git", "rev-parse", f"{version}^{{commit}}") != sha:
            raise ValueError("Release tag points to a different commit")
    else:
        # Create the actual tag explicitly, even while the release is a draft.
        run("gh", "api", f"repos/{repo}/git/refs", "--method", "POST",
            "-f", f"ref=refs/tags/{version}", "-f", f"sha={sha}")
    pages = json.loads(run("gh", "api", "--paginate", "--slurp", f"repos/{repo}/releases?per_page=100"))
    release = next((r for page in pages for r in page if r["tag_name"] == version), None)
    if release and not release["draft"]:
        print(f"{version} is already published; leaving its assets unchanged")
        return
    if release:
        # Retries can repair an incomplete draft, never a published release.
        run("gh", "release", "upload", version, *[str(directory / name) for name in assets], "--clobber")
        url = release["html_url"]
    else:
        url = run("gh", "release", "create", version,
                  *[str(directory / name) for name in assets],
                  "--verify-tag", "--draft", "--latest=false", "--generate-notes",
                  "--title", f"Music Controller {version}")
    print(f"Draft ready: {url}")
    if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(summary, "a") as stream:
            stream.write(f"## Draft release ready\n\n[{version}]({url}) · commit `{sha}`\n\n"
                         "Review release notes and test the assets, then edit the draft, "
                         "leave Pre-release unchecked, select Set as latest release and Publish release. "
                         "This workflow does not publish or deploy anything.\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["select", "prepare"])
    parser.add_argument("--version", default="")
    parser.add_argument("--directory", type=Path, default=Path("dist/release"))
    args = parser.parse_args()
    if args.command == "select":
        select(args.version)
    else:
        prepare(args.directory, args.version)
