# Forza Shader Lab

The stretching workaround is visually confirmed fixed by the user. This tool uses the same live
`skip.txt` mechanism that identified trees, leaves, stones, grass and walls. It requires the currently
installed shim and `HB_TSSHIM_HOT_SKIP=1`, which are already configured. No new DLL or game restart
is needed for vertex testing; pipeline tests refresh their inventory for the current game launch.

Open **Shader Lab.command** in Terminal. Start with menu **1** to review all loaded shaders, **2**
to identify objects through vertex shaders, or **3** to bisect for one particular object.
Keep Forza running in the scene you want to inspect. Return to Forza after each skip change and
wait about two seconds with the game unpaused. Tests stay active until you give a command; there
is **no automatic timeout by default**. Use **b** to restore the scene and **t** to reapply the test.
Submitting an observation or bisection answer restores the scene before advancing; **f** and **q**
also restore it immediately.

The initial inventory contains **3,255 stage/hash pairs across 17,128 pipelines**:
733 VS, 2,412 PS, 4 GS, 3 HS, 3 DS, and 100 CS. These are the shaders registered in the selected
Forza process, including variants that may not be used in the current view. Explore other locations,
weather, time of day, cars and menus, then choose **4** to refresh. Unloaded variants cannot be
catalogued from the current log. A shader with no visible change may be inactive, offscreen,
or used by a shadow/depth pass; that result does not verify its correctness.

## Test controls

- **Enter:** record what changed, then advance. Enter your object/effect description.
- **b:** show the full-scene baseline for comparison.
- **t:** reapply the current shader test until your next command.
- **f:** immediately restore the scene and mark a blank/frozen result inconclusive.
- **s:** skip this shader for now; it remains eligible in later sessions.
- **q:** quit and restore the exact original skip file.

During use, progress is saved under `data/` (including `observations.json` and `observations.csv`). These generated/local files are excluded from Git. The CSV stores your notes
alongside separate stage/hash columns, known label, matching pipeline count and IDs, the exact
skip-rule batches, observed census call count, test mode (review/bisect), completion status and time,
and the source game log/startup marker. Bisection confirmation notes use the same CSV. Each record
keeps the values from the launch in which you tested it, even after refreshing the inventory.
Completed shader reviews
are skipped on the next run. Use `--retest` or menu **5** to test one again. If you quit during a
multi-batch shader, its notes are retained and that shader remains pending. On resume its batches
restart so it can be compared in a consistent scene.

When generated, `data/inventory.csv` lists every shader hash, its stage, known object labels, matching pipeline
count and IDs, and VS/PS/CS call counts from the selected census. Those counts describe an earlier
recording interval, not necessarily your current view. The inventory also includes indexed-call and
fullscreen-triangle/quad-call counts plus the object-bisection eligibility flag. Historical labels for the five known vertex
hashes are included. The tool never assigns object names from shader hashes alone.

## How suppression works

Vertex shaders use stable `vs <hash>` rules, including direct and indirect draws. PS, GS, HS, DS and
CS reviews use `pso <index>` rules for every matching loaded pipeline. This stops the entire draw or
dispatch using that pipeline, including its other stages. It identifies a shader's participating
passes; it does not independently disable one stage inside a draw. Compute pipeline rules cover
both direct dispatch and the shim's ExecuteIndirect path.

For per-shader review, groups with more than 128 matching pipelines are split into successive
batches. Each batch stays active until your command. The tool never submits an overlong skip list that the shim
would silently truncate. PSO indices are specific to one game launch; refresh after restarting.

Object bisection uses only VS hashes observed in indexed draws in the selected census and excludes
hashes also seen in single-instance non-indexed triangle/quad draws. This is a heuristic to avoid
fullscreen/UI passes, not proof that every candidate is harmless to hide. Unobserved and non-indexed
object variants remain in the complete inventory and can be reviewed individually. The current
census yields **101 candidates**, including all five known object shaders.

Bisection starts with groups of at most **four** shaders. Answer **y/n** only when you can observe
the target normally. Use **f** if the view becomes blank or frozen: that restores the baseline and
subdivides the group without treating it as a positive or negative target result. An individual
inconclusive candidate is retained in the saved blocked list. **b** restores the baseline; **t**
reapplies the test; **q** exits. The final candidate still requires individual confirmation.
Round answers, hashes and exact rules are saved in `data/bisection-rounds.csv` and JSON. Final
confirmation notes go to `data/observations.csv`; blank/frozen results are marked `inconclusive`,
never `tested`.

The old large-group bisection temporarily skipped fullscreen rendering and left the previous image
on screen. The user confirmed that clearing its rules recovered Forza. Shader files are never
removed: the shim only skips matching draw/dispatch calls. If the GPU actually stops processing,
changing a file cannot force it to resume. A restoration command clears the rules, and the shim
applies the change at its next frame-boundary poll.

All-stage review visits one unique shader at a time. Shared shaders can affect many objects; pixel
shaders and compute shaders can affect the whole view. Compare against the baseline before recording.

## Terminal commands

The checked-in launcher and script live in `working-patches/04-shader-lab/`. Run these commands from that directory:

```sh
bash "Shader Lab.command"
python3 shader_lab.py inventory
python3 shader_lab.py review --stage all --refresh
python3 shader_lab.py review --stage vs --refresh
python3 shader_lab.py bisect --stage vs --refresh
python3 shader_lab.py bisect --stage vs --refresh --group-size 1
python3 shader_lab.py review --stage vs --shader a99ec1d9 --retest
python3 shader_lab.py restore
```

An automatic timeout remains available only if explicitly requested with `--pulse-seconds 12`.
Its default is `0`, which means manual restoration.

`review --stage vs --observed` or `bisect --stage vs --observed` limits candidates to the selected
census. The old `bisect_skip.sh` workflow and its `all-vs.txt` / `all-cs.txt` exports are not included in this Git archive. The current tester batches rules to stay within the shim's 128-rule limit.

## Restoration and recovery

Normal quit, Ctrl-C, and SIGTERM restore the **exact original bytes** of `skip.txt`, including a
missing original file. A journal is saved before any game-file change. After a hard kill or reboot,
choose **6** or run `restore`. Do not run another skip-list editor simultaneously. If another tool
changes the file, Shader Lab preserves that external change and retains the journal for inspection.
Inventory and tests read logs incrementally with bounded line memory, and log discovery reads only
bounded prefixes/tails. They do not load entire logs into memory.

Fifteen offline regressions cover inventory stages/latest launch, all pipeline batches, rule limits,
exact-byte restoration, exception handling, reboot recovery including journal/write interruption,
external-change preservation, baseline comparison, partial test notes, CSV quoting of multiline notes,
shader-detail snapshots across game launches, migration of existing notes, fullscreen filtering,
blank/frozen result handling, inconclusive-group subdivision, independent timer recovery and stale
timer cancellation. The archived test-output text file is excluded; rerun the test script locally if you need fresh output.
