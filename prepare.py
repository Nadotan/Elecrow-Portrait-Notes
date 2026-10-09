from pathlib import Path
import shutil
root = Path(__file__).resolve().parent
source = root / 'vendor/example/V1.2/Arduino_Code/Lesson09-LVGL_Lighting_Control'
if not source.is_dir(): raise SystemExit('Missing Elecrow vendor source; run the GitHub Actions workflow')
target = root / 'Sketch/Sketch'
target.mkdir(parents=True, exist_ok=True)
for f in source.iterdir():
    if f.is_file(): shutil.copy2(f,target/f.name)
ino=target/'Lesson09-LVGL_Lighting_Control.ino'
s=ino.read_text()
start=s.index('/* Button callback function - turn on LED */')
end=s.index('/*---------------------------------------------------------------\n * Arduino entry points',start)
s=s[:start]+s[end:]
s=s.replace('#include "lvgl_port.h"','#include "lvgl_port.h"\n#include "NotesUI.h"')
s=s.replace('    create_led_control_ui();','    Notes::start();')
(target/'Sketch.ino').write_text(s)
ino.unlink()
shutil.copy2(root/'NotesUI.h',target/'NotesUI.h')
# Improve responsiveness and release detection: poll the touch controller every LVGL read.
# Original vendor code returns early if there has been no touch interrupt; this may drop samples.
port=target/'lvgl_port.cpp'
s=port.read_text()
needle='''    if (tp->isInterruptEnabled() && xSemaphoreTake(touch_detected, 0) == pdFALSE) {
        return;
    }
'''
if needle not in s: raise SystemExit('Vendor LVGL input code changed; inspect before patching')
s=s.replace(needle,'    // Always poll touch to handle press, movement and release consistently.\n')
# Portrait orientation: raw touch readings are in the native 1024x600 coordinates.
# LVGL display rotates to 600x1024; map input into portrait coordinates too.
match='''        data->point.x = point.x;
        data->point.y = point.y;'''
if match not in s: raise SystemExit('Touch coordinate block has changed')
s=s.replace(match,'''        data->point.x = point.y;
        data->point.y = 1023 - point.x;''')
port.write_text(s)
print('Prepared',target)
