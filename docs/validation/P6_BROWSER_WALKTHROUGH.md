# P6 browser walkthrough

On 2026-11-17, the actual Face Studio at http://127.0.0.1:8765/ was exercised
through the in-app browser against the running local backend. The baseline
commit was 2842833bdfbc9f16bf83c78f6e65f9303e556dd0, with the container and
input-handler changes in this review follow-up. These observations cover
the browser controls and rendered canvas.

The timeline section now belongs directly to main and precedes the footer.
At a 1920×1080 browser viewport, main measured 1180 pixels wide and the
timeline panel measured 1108 pixels. The section's bottom was above the
footer's top. The temporary viewport override was reset after verification.

## Performed interactions

1. Loaded the sample clip and stepped from 0 to 16 ms. The preview reported
   three started events with zero rejected or skipped events.
2. Clicked **Add keyframe**. The inspector selected the new event, and the
   preview reported four started events.
3. Edited the new event through the inspector controls. Set Region to
   **Right eye**, From to **−1**, To to **2**, Easing to **Ease out**, and
   Priority to **5**. Renamed the clip to **Browser walkthrough** and set
   its identifier to **p6-browser-walkthrough**.
4. Clicked **Validate**. The badge showed **Validated**, and the backend
   reported a valid 336-byte asset. The grouped list showed the edited
   right-eye event.
5. Clicked **Export JSON** and **Export C++ asset**. The downloaded JSON
   included all six events and the edited values. Recompiling that JSON
   through the CLI produced byte-identical header text.
6. Scrubbed from **16 ms** to **5200 ms**, back to **0 ms**, and stepped to
   **16 ms** again. Screenshots cropped to the actual firmware canvas
   differed between 16 and 5200 ms. The screenshots before and after the
   backward seek were byte-identical at 16 ms.
7. Clicked **Import JSON** and chose the exported source. The badge showed
   **Validated import**. After stepping to 16 ms and selecting the right-eye
   event, the inspector retained Right eye, −1, 2, Ease out, and priority 5.
8. Clicked **Play**, observed **Pause**, paused playback, and restarted.
   Restart returned the scrubber to 0 ms and the preview to zero started
   events.

The first walkthrough found inspector values were only committed by
change handlers. The controls now update their model on input, and
the exported values above were verified after that correction.

## Retained evidence

The browser-exported source is tracked as
[p6-browser-walkthrough.json](../../scripts/tests/fixtures/p6-browser-walkthrough.json).

- Source SHA-256 is 683af326a042486da12efdf748020d4dee107d734d383ae482cbd4b1ec81cac5.
- Exported and CLI-generated header SHA-256 is
  c9e8e7bb051094ba4c0f606bbabd952c79c9e492a0ad195c67a9c2753c8d362d.

The browser screenshots were inspected during this walkthrough. The tracked
source and these recorded observations are the durable walkthrough evidence.

The additional handler regression test executes the actual editor JavaScript
with a small DOM fixture. It checks creation, all edited fields in the
compiler request, exported filenames, backward seek requests, and placement
inside main. Its backend replies are test fixtures. The browser interactions
above and the existing HTTP/firmware tests provide the integration evidence.

~~~sh
node scripts/tests/test-timeline-editor.js
node scripts/tests/test-face-studio.js
python3 scripts/timelines/compiler.py scripts/tests/fixtures/p6-browser-walkthrough.json
python3 scripts/check-timelines.py
~~~
