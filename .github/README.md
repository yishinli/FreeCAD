# FreeCAD, AI-maintained fork (unofficial)

This is a fork of [FreeCAD](https://github.com/FreeCAD/FreeCAD). Apart from two open FreeCAD pull
requests merged in at their authors' heads (listed in [FORK-CHANGES.md](../FORK-CHANGES.md)), its
changes are written, tested and reviewed by AI agents, and merged by a human maintainer. It is **not** affiliated with or
endorsed by the FreeCAD project. For FreeCAD itself, go to [freecad.org](https://www.freecad.org).

A small engineering team uses its builds every day to open and inspect large assemblies, so most
fixes here come from real files that went wrong: documents that open slowly, names lost on
read, rotations that drift after a save, crashes at exit in scripted runs, and a few viewing and
measuring tools.

## What is here

- **Branch [`test/section-both`](https://github.com/yishinli/FreeCAD/tree/test/section-both)**:
  FreeCAD `main` plus our changes. We merge upstream `main` into it regularly.
- **[FORK-CHANGES.md](../FORK-CHANGES.md)**: every change on the branch, grouped by feature, with
  its commit, what it fixes, its known limits and the build that first had it. Start here.
- **Branch `bp/26.3`**: a few of these fixes on top of the FreeCAD 26.3 release branch.

## Take what you need

Each of our fixes is its own commit with a message that explains the cause and the change, so it
can be cherry-picked on its own (`git cherry-pick -x <commit>`). The two FreeCAD pull requests are
merges; take those from the pull requests themselves. Everything is LGPL-2.1-or-later, like
FreeCAD. A mention of where it came from is appreciated.

If you want a fix in FreeCAD itself, read FreeCAD's
[AI policy](https://github.com/FreeCAD/FreeCAD/blob/main/AI_POLICY.md) first. It expects you to
fully understand, review and test what you submit, to disclose AI assistance, and to write the
issue, the PR and the replies yourself, and it does not accept clearly AI-generated code. The
commits here show where a problem is and one way to fix it; they are not ready to submit as they
are. We do not submit upstream on anyone's behalf.

## Contribute

AI-written and AI-assisted issues and pull requests are welcome here. See
[CONTRIBUTING.md](CONTRIBUTING.md) for the few rules: a test that fails before the fix, the build
and test commands you ran, and one fix per commit.

## Who answers

Replies on issues and pull requests in this repository are usually written by an AI agent (Claude)
working for the maintainer, on the maintainer's account. Merges are always decided by the
maintainer. Answers can take a few days.
