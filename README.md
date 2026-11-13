# Unreal Tangent Control Surface Plugin

Modified source for the [Tangent Control Surface Plugin](https://www.fab.com/listings/b0b0b6b1-6f0a-47b7-b5cd-cc2738ba1585), targeting Unreal Engine 5.6.

## nDisplay mappings

The Tangent Mapper **nDisplay** menu provides:

- **Entire Cluster:** color grading for Global, Shadows, Midtones, and Highlights.
- **Per-Viewport:** grading for an existing array entry selected with **Per-Viewport Group**.
- **Viewport Entry 0 / Viewport Entry 1:** independent bindings for the first two per-viewport grading entries. Missing entries reject edits.
- **ICVFX Camera 1 / ICVFX Camera 2:** independent camera banks. Each **Camera Component** selector lists the actor's available ICVFX components by name. Explicit selector choices are saved per project. A missing named component rejects edits instead of silently targeting another camera.

Each grading bank includes Saturation, Contrast, Gamma, Gain, and Offset, with **R/G/B/Y**, **H/S/V**, and **Combined** trackball/dial bindings. Combined mappings move the wheel with trackball X/Y and adjust brightness with the dial, preserving the separate Y/luminance value. Temperature, tint, and exposure compensation are also available.

ICVFX banks additionally expose **Enable Inner Frustum**, **Enable Inner Frustum Border**, **Enable Inner Frustum Chromakey**, and **Overscan Multiplier**. **Freeze Viewports** controls nDisplay's outer-viewport freeze setting.

Binding definitions remain static in `Tangent/Resources/XML/controls.xml`; camera targets and the shared per-viewport target are selected at runtime. Fixed viewport entries address array slots, so reordering the array changes which grading group occupies a slot. The ICVFX grading controls address each component's `CameraSettings.AllNodesColorGrading.ColorGradingSettings`.

Enable the relevant grading overrides, viewport assignments, and overscan settings in Unreal as usual. Reopen Unreal and reconnect Mapper after updating the plugin definitions. Existing mappings retain their IDs.

## Property mapping and reliability fixes

- Training supports nested nDisplay configuration objects, per-viewport arrays, and actor-owned components such as F1/F2.
- HSV sliders can be trained directly. The obsolete **Use HSV Controls for Color Wheel Movement** option and its reinterpretation of H/S inputs have been removed; H/S/V retain their normal meanings and Combined mappings handle wheel movement.
- Property edits notify the actual owning objects and complete editor transactions, including indexed and component paths.
- Property addresses are re-resolved after reconstruction, array changes, and undo/redo.
- Project settings include property-limit enforcement, transaction completion timing, and automatic actor-class mode rules.
- Communication, game-thread access, light-color caching, save-time input handling, and property-cache cleanup retain the existing reliability fixes.

## Build and validation

Place `Tangent` in a project's `Plugins` folder and build with the Unreal Engine 5.6 Windows Editor toolchain. nDisplay must be enabled for the nDisplay regression tests. The source requires the original Tangent plugin environment and Tangent Hub/Mapper for hardware operation.

