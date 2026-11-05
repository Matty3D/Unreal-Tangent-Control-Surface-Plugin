# Tangent 1.1.5-local.1

Project-local changes based on Tangent 1.1.5. The engine installation is unchanged.

## Changes

- Reflected property mappings retain the object that actually owns the value, all object boundaries, the property chain and dynamic array indices.
- Panel edits call Modify/PreEditChange and interactive/final chained notifications on the affected objects, including referenced configuration UObjects.
- Transactions finish after the existing 0.5-second idle interval. Completion retries during saves or nested transactions, and only closes the transaction opened by Tangent.
- Training uses the post-change chain event, which retains array index metadata. Example mapping: `CurrentConfigData.StageSettings.PerViewportColorGrading[0].ColorGradingSettings.AutoExposureBias`.
- Property addresses are resolved on every access to avoid stale pointers after array reallocation, undo, reconstruction or configuration replacement. Missing array entries are rejected.
- New struct array entries without an archetype counterpart use an initialized struct default for reset.

Array mappings address a slot by index. Reordering or removing earlier entries changes which entry occupies that slot; retrain affected mappings after those operations. Maps, sets and static indexed arrays are outside this patch.

## Validation

Windows Editor build checked with the installed Unreal Engine 5.8 toolchain. Automated checks are registered as `Tangent.Properties.ArrayTrainingAndResolution` and `Tangent.Transactions.NestedOwnerAndCompletion`. They must run in an idle editor; they exercise a temporary world and editor undo/redo.

Run-time validation is still required: the separate test-editor process exited without producing a report in the tool environment. A successful build is not a passing test run or proof of Multi-User replication.

## Stage verification

1. Start the editor with this project-local plugin and verify the version is `1.1.5-local.1`.
2. Train the per-viewport exposure control. The logged mapping must contain the edited slot, e.g. `PerViewportColorGrading[0]`.
3. Adjust the control, stop for at least 0.5 seconds, and look for `Tangent: completed panel transaction` in the output log. Confirm undo/redo restores exposure.
4. In Multi-User, compare the configuration value on a second client after adjustment and release. Then verify the running nDisplay cluster through the stage's normal Multi-User workflow.
5. Check the original entire-cluster binding as well. All clients loading the plugin should use the same patched version.

The source backup and build/test logs are under the project's `work/TangentFix` folder. Keep Unreal closed when replacing plugin DLLs. Restore the backup source and original DLLs to roll back, or move the project-local plugin out of `Plugins` while the editor is closed to use the installed engine version again.
