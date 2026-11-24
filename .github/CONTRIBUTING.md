# Contributing to this fork

This fork accepts AI-written and AI-assisted work: issues, pull requests, comments and commit
messages may all come from an AI agent. The rules are about evidence, not about who typed it.

These rules apply only to this repository. For FreeCAD itself, FreeCAD's own
[CONTRIBUTING.md](../CONTRIBUTING.md) and [AI_POLICY.md](../AI_POLICY.md) apply.

## Issues

- Say which build or commit you ran, the operating system, and the steps that show the problem.
- Attach the smallest file that shows it, or a script that builds one. Do not attach files you
  are not allowed to share.
- If the problem also happens in an official FreeCAD build, say so; we check whether it comes from
  a change of ours.

## Pull requests

1. Target `test/section-both`.
2. Code fixes are test-first: add or change a test that fails without the fix, then make it pass.
   If a test is not practical (GUI only, timing), give a script or steps that show the failure
   before and its absence after.
3. In the PR description, list the build and test commands you ran and their results.
4. One fix per commit, each commit builds on its own, and each message says what was wrong, why,
   and what changes. This keeps every fix easy to cherry-pick.
5. Keep each file's line endings (some files use CRLF).
6. Disclose AI help with a trailer, `Assisted-by: <model>` or `Co-Authored-By: <model>`.
7. Do not include anything you may not publish: customer or company data, part numbers, secrets.

A pull request is reviewed (often by an AI reviewer first), may get change requests, and is merged
by the maintainer. A merged change gets a line in [FORK-CHANGES.md](../FORK-CHANGES.md).

## License

Contributions are accepted under the license of the files they change (LGPL-2.1-or-later for
FreeCAD sources). By opening a pull request you confirm you have the right to submit it under
that license.
