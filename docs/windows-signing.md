# Windows installer signing and update trust

Music Controller Setup is a PyInstaller-built Windows executable. Unsigned builds may
show Windows Defender SmartScreen's **Windows protected your PC** warning.
Authenticode signing proves publisher identity but **does not guarantee** SmartScreen
will stop showing warnings: reputation is evaluated separately.

## SignPath Foundation application

The project is Apache-2.0 licensed, public, built in GitHub Actions and publishes its
release artifacts from a controlled pipeline. Apply at https://signpath.org/ for
Foundation sponsorship, subject to their eligibility and review.

Maintainers must complete project registration, ownership verification, required
account protection and approval requirements. Approval and a signing certificate
**cannot be issued by this repository or its CI workflow alone**.

Following approval, configure the SignPath organization/project/signing-policy and
a protected signing token in GitHub Actions. Wire the signing action to the release
pipeline **after** the Windows build, with the signed output replacing the unsigned
release asset before SHA256SUMS/release checksums are generated. Keep signing
credentials out of PR workflows. Verify Authenticode with PowerShell:
`Get-AuthenticodeSignature .\music-controller-setup.exe`.
Do not claim release binaries are signed before verification.

## Updates

Only published stable releases are considered for application self-updates.
The executable embeds its **own** release version, independent of selectable
firmware versions. Update assets must expose a GitHub SHA-256 digest matching
the downloaded EXE. A verified replacement is staged in the user temp directory;
the running executable exits before its replacement is installed.

The firmware manifest can declare `installer.minimum_version`, e.g.
`"installer": {"minimum_version": "0.5.8"}`. This prevents an old installer
from flashing a release requiring newer layout/schema support. It does *not*
force updates when the user deliberately selects an older compatible release.

Security limitation: a SHA-256 digest fetched from the same GitHub API as an
asset provides corruption/tamper detection but not an independent publisher
identity guarantee. Once SignPath approval is granted, pin and verify the
expected Authenticode publisher on downloaded updates before execution.
Until then, downloads should be considered GitHub-release-trusted, not
certificate-authenticated.

Development builds have version 0.0.0 and do not self-update.
