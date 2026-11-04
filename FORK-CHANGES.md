# Fork changes (yishinli/FreeCAD, branch test/section-both)

What this branch adds on top of FreeCAD main, sorted by feature, with the build each change first
shipped in and the limits we know of. It is the summary; each commit message is the full record of
its change (`git log <commit>`).

- Base: FreeCAD main at merge base `3cd29aa88e`.
- Merged open upstream PRs (not ours): #28647 Part::SectionAnalysis (head `d2e5af9371`) and #33186
  section caps on the clipping plane (head `6985eb3a8d`).
- Everything else here is fork only. Nothing here is proposed upstream; the fork owner writes any
  upstream report personally.
- Builds go to coworkers as "FreeCAD 27.1 dev", for opening and reading files only. FreeCAD 26.3
  stays the version for design work. Each build ships a BUILD-INFO.txt that lists what it holds.
- Ranges such as `a` … `b` (inclusive) include both ends; in git that is `a^..b`.

## For tools and agents that poll this file

- Watch the branch: `git ls-remote https://github.com/yishinli/FreeCAD refs/heads/test/section-both`.
- When it moved: `git fetch` it and read `git diff <last seen>..FETCH_HEAD -- FORK-CHANGES.md`, then
  `git log <last seen>..FETCH_HEAD` for the commits themselves.
- If `git merge-base --is-ancestor <last seen> FETCH_HEAD` fails, or `<last seen>` can no longer be
  fetched, the history was rewritten (a rebase or reword): read the whole file again instead of the
  diff.
- Every push that changes behaviour updates this file in the same push (a follow-up docs commit in
  that push is fine, as a commit cannot name its own hash): a new section or a new line under "Not
  built yet". A build moves its lines from "Not built yet" to the build's name.
- A rebase or reword rewrites the hashes in this file in the same push. A change to the hashes only
  means history was rewritten, not that behaviour changed.
- Requests, approvals and status between people or agents do not belong here; this file only
  describes the code on this branch.

## Builds

| Build (VERSION.txt) | Date | Fork changes it first had |
|---|---|---|
| freecad-main-20261029-section-step4 | 2026-10-29 | first coworker build, the two PRs and `7f9c5adc51` … `e8f37d3b49` (inclusive) |
| freecad-main-20261030-section-readonly | 2026-10-30 | `45f8b8c36a` … `95b8086053` (inclusive) |
| freecad-main-20261031-navstyle | 2026-10-31 | `a105eb7600` … `8b806e6c00` (inclusive) |
| freecad-main-20261031-export-guard | 2026-10-31 | `1d66a5ee57`, `9bcec42850` |
| freecad-main-20261101-measure | 2026-11-01 | `8eb1bb2a4a` … `395ac57d86` (inclusive) |
| freecad-main-20261101-explode | 2026-11-01 | `eddd58c478` … `2fd61741ee` (inclusive; latest coworker build) |
| Not built yet | | `d351d40155` … `7788ede697` (inclusive), `7e28530ed7`, `8108d30fb8`, `224b77e515`, `3758746687`, and this file |

## Section Analysis

View > Section Analysis cuts the model with a plane, caps the cut and can show several sections.
The feature is PR #28647; the clipping-plane caps (View > Clipping View > "Section caps") are
PR #33186. Our changes on top:

- `9202380ae2`, `4a6777b1d4` Part: `{}` placeholders in SectionAnalysis warnings.
- `718c7ee9f0` Part: holes in a solid's mesh are bridged, so its cap still fills; the remesh uses
  the source's own Deviation and AngularDeflection.
- `5ca85b327a` Part: section-cap solids are meshed on all cores (30.6 of 31.6 s on a 941-part
  assembly before); solids that share a face or an edge stay on one thread.
- `249a114dbf`, `ab9b7a3253` Part: "Align to Face or Edge" aligns the plane to a picked face, edge or
  datum.
- `e8f37d3b49` Part: a face lying on the plane is capped, not hollow.
- `45f8b8c36a` Part: the panel shows the preset and tilt the plane really has, also when it was set
  from the property editor or a script.
- `b1a4eed231` Part: making, moving, editing or deleting a section does not mark the document
  modified (object status NoModify, honoured by Gui::Document).
- `d579f512c6`, `6af9832adf` Part: the plane handles stay where the user put them or picked.
- `a105eb7600` Part: showing or hiding a section source recomputes only the section and leaves the
  document unmodified.
- `cd260cd115` Part: a section brought back by undo follows source visibility again.

First built: `9202380ae2` … `e8f37d3b49` (inclusive) in section-step4; `45f8b8c36a` …
`6af9832adf` (inclusive) in section-readonly; `a105eb7600` and `cd260cd115` in navstyle;
`4a6777b1d4` in explode.

## Read-only viewing

- `c056c62be9`, `b74100338a` App: started with `--set-config OpenReadOnly=1` (the coworker launcher
  does this), a document cannot be saved back over the file it was read from; the tree shows a
  read-only mark. The check fails closed when the file system cannot tell.
- `1d66a5ee57`, `9bcec42850` Gui: Save As, File > Export, mesh export and the other export dialogs
  refuse the name of a file opened read-only ("File is read-only").

Known: only files opened through the launcher are protected, not a .FCStd opened by double-click
(that opens 26.3); Python or macros that write files directly, or use QFileDialog themselves, are not
checked; the export check only knows the files of documents that are open; the Qt replace
prompt shows before our message.

First built: section-readonly (open), export-guard (export).

## Isolate, navigation and shortcut styles

- `3a23f4ca04`, `327ef0c2b4` Gui: Std_IsolateSelection (right-click menu, View > Visibility) hides
  everything but the selection and shows again what it hid; it can restore while a task is open.
- `bd5540f1e1`, `16fd2b7402` Gui: SolidWorks navigation: right drag orbits (also while spinning and
  in a section edit), middle double click goes home.
- `131fd0280c`, `8b806e6c00` Gui: Preferences > General > Keyboard > "Shortcut style" switches all
  shortcuts at once (FreeCAD, SolidWorks).
- `395ac57d86` Gui: in the SolidWorks style, Shift+M opens the company FusionStyle macro's Smart
  Measure once the macro has run.
- `5acf9b72c1` Gui: a Fusion shortcut style: the SolidWorks style's keys plus Shift+N (Part Colors).

First built: navstyle; Shift+M in measure; Fusion style in explode.

## Measuring and preferences

- `8eb1bb2a4a` Part: Apply in Preferences no longer resets every Part object's deviation and marks
  documents modified when nothing on the page changed.
- `5a4dba1093` Measure: measuring leaves the document unmodified unless a measurement is saved.
- `3208146c40` Measure: a distance to a hole, boss or half hole is measured from its axis.
- `8c70a3c49e` Measure: a zero dx/dy/dz is not drawn over the others.
- `2bb07d0b93` Measure: closing Std_Measure without saving leaves no empty undo step.
- `faff898899` Measure: Mass Properties closed by a macro removes its preview.

First built: measure; the last two in explode.

## Inspection: interference and parts list

- `eddd58c478`, `bd062ebd07`, `ac8be50449`, `0cd1553037`, `39ac184f50`, `2718c63b04` Inspection >
  Interference finds visible parts that share volume (red), touch (yellow, optional) or come closer
  than a clearance (cyan): all parts, the selection against the rest, or within the selection.
  Results are drawn while the panel is open; nothing is added to the document. Gaps and volumes are
  each listed in one unit; pairs are measured on several threads.
- `5578dfa3b7` Inspection > Parts List: one line per shape with quantity, size and volume; copies
  count together even as separate objects (imported STEP); mirror images apart; Find Copies; Save as
  CSV.

Known (interference): mesh objects are not checked (listed as unchecked); the 240 s limit is checked
between pairs, so one slow pair can run over it; results are drawn only in the 3D views open when
Check was pressed; an OCC crash while intersecting two parts closes FreeCAD; a macro that changes
parts during the check is not supported.
Known (parts list): congruent parts modelled separately can be listed as "(mirrored)"; axisymmetric
left- and right-hand parts are not told apart; a hidden part only referenced by a sketch or datum is
not counted with hidden parts on.

First built: explode.

## Part Colors and Exploded View

- `784c1172ad`, `748080d730` Gui: Std_PartColors (View > Visibility; Shift+N in the Fusion style)
  shows each part in a colour of its own, link copies included, and back. Only the 3D view changes.
- `2fd61741ee` Gui: Std_ExplodedView (View > Visibility) moves the components apart with a slider,
  the largest one staying, and puts them back on closing. No property is written.

Known: an assembly isolate, a joint highlight or a link re-applying its colours can clear Part
Colors there until the next refresh. In an exploded view, picking hits the moved parts while
measuring, interference and the parts list use the real places; Section Analysis caps stay at the
real shapes; a link array, and a part inside a sub-assembly linked more than once, move as a whole.

First built: explode.

## Reload a document changed on disk

- `d351d40155` Gui: when another program rewrites the file of an open document, a bar above its views
  offers Reload or Ignore (orange, "discards your unsaved changes", when there are some). Reloading
  restores from a copy of the file, keeps the camera and the read-only state, and reloads changed
  linked files first. Preferences > General > Document > "When a file is changed by another program":
  ask (default), reload when nothing is unsaved, or nothing.
- `ff1514baee` Gui: Save and Save All ask first when the file changed on disk since it was opened,
  reloaded or saved here; the other version is kept as a backup file when backups are on.
- `8108d30fb8` Gui: on Windows, on a local NTFS disk, the watched file is read with every share mode,
  so a FreeCAD saving it (backups on or off) is not refused while it is checked or copied for a
  reload. On a network share it is read as before: a save during that moment is refused and the
  file kept (sharing delete there would let a save with backups off lose the file). Programs that
  rename a new file over the old one in one step (most editors) are refused for that moment
  everywhere, as Windows refuses them while anything reads the file.
- `224b77e515` Gui: turning the setting back on, from the preferences or a macro, shows at once a change
  made while it was off.

Known: only one 3D view comes back after a reload, with its tab at the end; a file rewritten by
FreeCADCmd has no GuiDocument.xml, so colours and visibility come back as defaults; an edit to a
document that the reload already touched, made while the next file is being copied, loses its
modified flag (not its content), so closing would not ask to save it; quitting during a reload that
waits for a panel can leave the temporary copy behind.

Not built yet.

## Opening large assemblies faster

Measured on a production assembly (11,645 objects, 479 shapes; Linux, 36 threads): open 27.6 -> 17.4 s, waiting
for the tree after opening 3.1 -> 1.1 s; memory after opening 1.83 GB with the first of these
changes, 1.48 GB with all of them (1.9 GB peak before them). Nothing shown changes: the same meshes,
icons and colours.

- `6ebe7a3761` PartGui: shapes of a document being restored are meshed together, one per thread,
  then drawn as before; a document without GuiDocument.xml is still fit to the view; adds
  `Gui::Document::restoredGuiDocument()`.
- `6a0cd0733a` PartGui: points of mesh nodes no triangle or edge uses start at zero instead of
  uninitialised memory.
- `0f9e5f1983` Gui: pixmaps of FreeCAD's own SVG icons (up to 128 px, no colour mapping) are kept, so
  the tree does not re-render the link arrow for every link. Without this change, as upstream,
  LinkInfo's icon map gains an entry on every icon request; with it, the map is hit.
- `6bfdeaba8e` Gui: the placement indicator (ShowPlacement) is built when it is first shown, not for
  every geometry view provider (1.3 s and 320 MB on that assembly).
- `7788ede697` App: the default appearance is read once per restore instead of once per view
  provider.

Tip, no code: saving with the hidden preference `BaseApp/Preferences/Document/SaveBinaryBrep = true`
stores shapes in binary (BinTools version 3, read by older FreeCAD too) and opened that assembly a further
3.8 s faster with the same meshes; but everyone who saves the file must have it set, or the next
save writes text again.

On Windows (an 8-core laptop, `58a0025272`; another program kept the CPU at 93–99%, so the open
wall time, about 60–75 s, did not compare): CPU time to open 80.5 and 83.0 -> 65.6 and 68.9 s,
waiting after opening 14–33 -> 2–4 s, memory after opening 2.07 -> 1.72 GB, peak 2.37 -> 2.03 GB,
the first View Fit 26.2 and 3.5 -> 2.0 and 1.7 s.

Known: a machine with fewer cores gains less on meshing.

Not built yet.

## Reading documents

- `3758746687` Base: a non-ASCII character in Document.xml that a 48 KiB read cut in two is no longer
  read as "???" (since upstream 3728496097). Names and labels on such a boundary came back wrong;
  with a document loaded partially through a link from another document, an object name broken
  this way in the dependency list left that object's children unloaded.

Not built yet.

## Build fixes and CI (no user-visible change)

- `91b5b35188`, `6cb024f845`, `95b8086053` MSVC build fixes (WireJoiner include, std::string in a
  test, a local named like the Windows `near` macro).
- `7f9c5adc51`, `feedb04e34`, `a60e041b25`, `ec6182dc5f` CI: Windows build on push to this branch,
  uploaded as a runnable build (local test only, not for upstream).
- `7e28530ed7` Gui: the shortcut style file comments name only the program each style follows.

First built: section-step4; `95b8086053` in section-readonly; `7e28530ed7` not built yet.
