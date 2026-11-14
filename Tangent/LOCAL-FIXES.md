# Tangent 1.1.5-local.1-ue56

Source changes based on Tangent 1.1.5. This checkout targets Unreal Engine 5.6.

## Reflected property edits and training

`PropertyData` retains each object boundary, the reflected property chain, dynamic array indices, and initialized defaults for new struct entries. Lookups resolve current storage on every access. Indexed mappings address array slots; missing entries reject edits.

`UEngineHelper` calls Modify/PreEditChange and interactive/final chained notifications on the actual value owners, including configuration UObjects and actor-owned components. Transactions complete after the configured idle interval, retry during saves or nested transactions, and only close the transaction opened by Tangent. UE 5.6's Details finished-edit training route retains the original array indices. Explicit `@ComponentName` paths distinguish actor-owned components without reflected references, such as F1/F2.

## Native nDisplay controls

`NDisplayGrading.cpp` resolves nDisplay vectors and scalars through `CurrentConfigData.StageSettings`. Mapper exposes **Entire Cluster Color Grading**, **Per Viewport Color Grading Entry 0**, and **Per Viewport Color Grading Entry 1**. The shared selector-based Per-Viewport group is removed from the available menus; its legacy command handling remains for compatibility. The two fixed-entry banks address array slots independently.

Each ICVFX bank targets the first or second ICVFX component sorted by component object name, resolved through `@ComponentName.CameraSettings`. Grading uses `AllNodesColorGrading.ColorGradingSettings`. Additional native fields are `bEnable`, `Border.Enable`, `Chromakey.bEnable`, and `CustomFrustum.FieldOfViewMultiplier`. Camera Component selector menus, saved-target overrides, and their protocol handlers have been removed. A bank with no corresponding camera rejects edits.

Combined bindings bundle three existing-style wheel parameters. They reuse the plugin's wheel orientation, saturation-radius conversion, inverted vertical direction, brightness limits, and offset translation. nDisplay edits preserve vector W and use the reflected-property notification/transaction path. Reset baselines are separated by actor, property path, and axis; individual parameters use per-path reset identities so changing the selected viewport or camera does not transfer reset values to a different property.

Individual R/G/B/Y and H/S/V controls coexist with Combined bindings. Temperature, tint, and exposure compensation use the native white-balance/exposure fields. Freeze Viewports targets `CurrentConfigData.StageSettings.bFreezeRenderOuterViewports`.

Mapper definitions contain **100 Combined bindings, 1017 parameters, and 7 menus** within nDisplay. Existing IDs are preserved. Independent viewport banks use high-byte tags 1/2; camera banks use 3/4. This plugin does not append actor-specific binding groups to Mapper at runtime.

## Removed HSV wheel reinterpretation

Removed `bHSVColorWheelMovement`, its project-settings UI, the custom HSV-channel movement branch, and the legacy CCR movement branch. Normal H/S/V values remain available. Use Combined mappings for wheel movement. The obsolete movement test was replaced by `Tangent.Properties.ColorCorrectionRegionNativeHSV`.

## Validation

Windows / Unreal Engine 5.6 build and all **14 Tangent automation tests passed with zero test warnings** on November 13, 2026. The real nDisplay fixture exercises indexed targets, named F1/F2 components, independent edits, missing-target rejection, native scalars/toggles, Combined movement, W preservation, reset isolation, configuration notifications, and undo/redo. Existing property, training, and reliability tests also pass. XML IDs and Combined member references were validated.

Reopen Unreal and reconnect Tangent Mapper after installing an updated plugin. Keep the existing grading enables, overrides, viewport assignments, and overscan setup appropriate for the scene. Verify the new bindings with physical panels and the running cluster, then check Multi-User propagation through the stage's usual workflow. The automation results do not establish hardware or Multi-User validation.
