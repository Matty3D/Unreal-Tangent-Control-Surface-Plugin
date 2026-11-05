Modified version of the Tangent Control Surface Plugin
https://www.fab.com/listings/b0b0b6b1-6f0a-47b7-b5cd-cc2738ba1585


Bugfixes:

 It's now possible to bind correctly to any property on the nDisplay blueprint:
- Fixed bug where bindings to nDisplay actor properties wouldn't finish the transaction, so you wouldn't see the change until next action.
- Fixed bug where you could not bind to properties in nDisplay's Per-vewport color grading array
- Can now bind to inner frustum color grading - now that they've been moved to the color grading window.
- Fixed a crash, a cached camera reference was keeping the old level alive during cleanup.
- Can now bind to HSV sliders as well as RGB for color correction.
- New Plugin settings under Project settings/Tangent, You can turn on an option so that the plugin respects property min/max values. Clamping the range.
- Another new plugin setting that makes the HS mappings in HSV move around the color wheel using the tracking balls in the direction you move in, rather than direct bindings to the raw values.

The following issues were also fixed:

**Socket reconnects can race with outgoing messages.**

The background thread closes and deletes the socket while editor callbacks can call SendToHub() using that same pointer. There’s no synchronization around its lifetime. This creates a potential crash when the Hub disconnects during an edit. Sends are also blocking, so a stalled Hub could freeze the editor.
Suggestion: Give the communication thread sole ownership of the socket and queue outgoing messages. Handle partial sends rather than only logging them. 

**Some actor access still happens on the background thread.**

Incoming commands call MoveSelected(), RotateSelected(), and ScaleSelected(), which read the selected actor and update reset caches before scheduling the actual change on the game thread. Selection changes and world cleanup can happen concurrently. This leaves another potential crash path during map reloads.
Suggestion: Move the entire operation—including selection lookup, reading values, and reset-cache access—onto the game thread. 

**Light colors can carry over from the previously selected light.**

HSVColorCache stores one color for 500 milliseconds without recording which light it belongs to. Adjusting light A, then quickly selecting and adjusting light B, can apply A’s cached color to B.
Suggestion: Associate the cache with a weak light reference and invalidate it when the light changes or its color is edited externally. 

**Autosaving can discard button releases and connection messages.**

ParseReceived() drops the entire incoming packet while saving. That includes button releases, handshake messages, and mode updates—not just property edits. A release lost during autosave could leave a raw-input button logically held.
Suggestion: Keep processing connection and button-state messages during saves; defer or suppress only scene edits. 

**The property cache grows without cleanup.**

Each new object/property combination allocates a PropertyData with new. I found no matching deletion or cache eviction, including during world cleanup or helper destruction. Repeated level reloads and actor reconstruction can accumulate unused entries.
Suggestion: Use owned cache entries and remove entries for expired objects or unloaded worlds. 
