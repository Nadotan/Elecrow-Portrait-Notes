# Elecrow Portrait Notes — experimental firmware

For **Elecrow CrowPanel Advanced 9-inch ESP32-P4 V1.2** with screen physically rotated 90 degrees clockwise.

This is an **original notebook interface inspired by tablet note apps**, not a copy of Samsung Notes and not an official Samsung product.

## Features
- Portrait home screen; create and reopen up to six named notebooks
- Blank, ruled, or graph paper; 12 pages per notebook
- In-note toolbar with pen colors, marker, eraser, undo, next/previous page and hide/show toolbar
- Touch coordinate mapping for portrait, continuous stroke interpolation, input polling
- Auto-save after 1.6 s of inactivity + manual save (SPIFFS); notebook metadata in NVS
- ESP32-P4 PSRAM enabled in GitHub Actions build

## Important limitations
- **Untested on physical hardware.** A successful compile does not guarantee LVGL rotation support with the vendor driver. Test one step at a time.
- GT911 is a capacitive touch controller, **not S Pen**, pressure sensitivity and palm rejection unavailable.
- The toolbar is similar in purpose but **not identical** to Samsung Notes. Lasso, handwriting-to-text, PDF markup, complex zoom, keyboard editing and real highlighting are not included in this beta.
- Eraser writes white and therefore will also cover lines/grid underneath. Mark tool uses opaque yellow, not true alpha blending.
- SPIFFS is not auto-formatted to preserve existing data. **No automatic formatting**. If no SPIFFS partition exists, saving fails. Choose a validated SPIFFS partition scheme before relying on persistence.
- NVS and SPIFFS data can be overwritten by full merged firmware flashes, repartitioning or erase-flash. Back up important pages before flashing.
- The current canvas is a finite 568x770 drawing area. Larger strokes are clipped to the page.
- Only the first 24000 touch samples per page are retained; saved files can fill available SPIFFS. MAX_BOOKS=6, MAX_PAGES=12.
- Creating a notebook does not open the Android on-screen keyboard; by default the name is “My notebook”. Text entry requires supported LVGL keyboard overlay and is not yet implemented.
- This app uses only on-device flash, no network or cloud sync.

## Build without Arduino IDE
1. Create a **new GitHub repository** (do not overwrite your working old repo).
2. Upload the contents of this ZIP *into the repository root*, including `.github/workflows/build.yml`.
3. Actions > `Build Elecrow Portrait Notes V1.2` > Run workflow.
4. Confirm build success and download the artifact. Do not flash until you verify both orientation and partition settings.
5. For merged firmware use offset `0x0`, with flash parameters appropriate to your device. **Keep an old known-good firmware artifact for recovery.**

## Source/build method
`prepare.py` fetches and modifies Elecrow's official V1.2 Lesson09 vendor example in GitHub Actions. The official files are used for the power rails, panel driver, and LVGL port; the custom UI code is `NotesUI.h`.

## Storage troubleshooting
A successful build doesn't prove storage works. If `Storage unavailable` appears, stop and check actual partition table. Do not assume `SPIFFS.begin(true)` is safe: it may erase storage. No partition override is provided in this beta because board flash size and actual partition layout must be validated before flashing.
