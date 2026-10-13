# Earlier graphics investigation (11 October 2026)

This is a historical summary of the vertex-input investigation. The raw probe output, runtime logs,
capture directories, deployment scripts, and grass/water experiment artifacts from that session are
not included in the Git archive. The report's original paths referred to local diagnostic folders
and are not reproducible from a checkout.

The investigation found 124 mismatches across 384 component checks when APPEND input offsets were
passed through unchanged. Explicit offsets and the deployed shim each produced zero mismatches in
the recorded probe. The user confirmed the stretching was fixed. These results support the input
layout workaround; they do not establish that all rendering issues share the same cause.

The current source and focused verification programs are in `../02-rendering/source/` and
`../02-rendering/verification/`. The current package and install procedure are described in
`../02-rendering/README.md` and `../APPLY.md`. For the separate road-discovery crash investigation,
see `world-entry-veh-bypass.md`.
