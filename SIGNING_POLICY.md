# Code signing policy

Free code signing provided by SignPath.io, certificate by SignPath Foundation.

This repository is preparing its Windows release pipeline for SignPath Foundation Open Source Code Signing. Release binaries are built from the public source repository and, once the project is accepted, signing will be restricted to verified builds produced from the release branch.

## Roles

- Committer and reviewer: [glitchaka](https://github.com/glitchaka)
- Approver for release signing requests: [glitchaka](https://github.com/glitchaka)

Changes submitted by contributors who do not have commit access must be reviewed before merge. Project maintainers are required to use multi-factor authentication for GitHub and SignPath accounts.

## Release signing

- Source repository: https://github.com/glitchaka/chats-gamebased
- Release branch: `main`
- Signed Windows artifact: `chats-gamebased.exe`
- Product name: `chats-gamebased`
- Signing requests must originate from the automated GitHub Actions build associated with the source commit.
- Every release signing request requires manual approval through SignPath.

## Privacy

The project's network behavior is documented in [PRIVACY.md](PRIVACY.md).

The application does not include telemetry, advertising, analytics or a remote account service. It communicates directly with peers chosen by the user and uses a STUN service only to determine the public UDP endpoint required for direct peer-to-peer communication.
