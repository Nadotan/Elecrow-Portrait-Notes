from pathlib import Path
import shutil
root = Path(__file__).resolve().parent
source = root / 'vendor/example/V1.2/Arduino_Code/Lesson09-LVGL_Lighting_Control'
if not source.is_dir():
    raise SystemExit('Missing Elecrow vendor source; run GitHub Actions')
target = root / 'Sketch/Sketch'
target.mkdir(parents=True, exist_ok=True)
for f in source.iterdir():
    if f.is_file():
        shutil.copy2(f, target / f.name)
ino = target / 'Lesson09-LVGL_Lighting_Control.ino'
s = ino.read_text()
start = s.index('/* Button callback function - turn on LED */')
end = s.index('/*---------------------------------------------------------------\n * Arduino entry points', start)
s = s[:start] + s[end:]
s = s.replace('#include "lvgl_port.h"', '#include "lvgl_port.h"\n#include "NotesUI.h"')
s = s.replace('    create_led_control_ui();', '    Notes::start();')
(target / 'Sketch.ino').write_text(s)
ino.unlink()
notes = (root / 'NotesUI.h').read_text()
notes = notes.replace('storage=SPIFFS.begin(false);restore_books();', 'storage=SPIFFS.begin(true);restore_books();')
if 'storage=SPIFFS.begin(true);restore_books();' not in notes:
    raise SystemExit('NotesUI.h storage code has changed')
(target / 'NotesUI.h').write_text(notes)

# The Elecrow V1.2 example defaults to DIRECT rendering. LVGL 9 does NOT
# rotate direct framebuffers as required by a 90-degree portrait app.
# Switch to PARTIAL rendering and rotate each flushed area into native
# 1024x600 LCD coordinates before submitting to the MIPI-DSI driver.
header = target / 'lvgl_port.h'
h = header.read_text()
old = '#define LVGL_PORT_AVOID_TEARING_MODE            (3)'
if old not in h:
    raise SystemExit('Elecrow LVGL mode configuration changed')
h = h.replace(old, '#define LVGL_PORT_AVOID_TEARING_MODE            (0)')
header.write_text(h)
port = target / 'lvgl_port.cpp'
p = port.read_text()
start = p.index('#else\nstatic void flush_callback(')
end = p.index('\nIRAM_ATTR static bool on_draw_bitmap_finish_callback', start)
replacement = '''#else
// Persistent rotated buffer: drawBitmap can complete asynchronously for DSI.
// Allocate once, and keep it alive until the LCD flush completion callback.
static uint16_t *portrait_flush_buf = nullptr;
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    LCD *lcd = static_cast<LCD *>(lv_display_get_user_data(disp));
    const int src_w = area->x2 - area->x1 + 1;
    const int src_h = area->y2 - area->y1 + 1;
    const uint16_t *src = reinterpret_cast<const uint16_t *>(px_map);
    // LVGL begins flushing in native landscape (1024x600) before
    // Notes::start() requests portrait rotation. Handle BOTH cases.
    int native_x, native_y, native_w, native_h;
    const uint8_t *output = px_map;
    const lv_display_rotation_t rotation = lv_display_get_rotation(disp);
    if (rotation == LV_DISPLAY_ROTATION_0) {
        native_x = area->x1;
        native_y = area->y1;
        native_w = src_w;
        native_h = src_h;
    } else if (rotation == LV_DISPLAY_ROTATION_90) {
        native_x = 1023 - area->y2;
        native_y = area->x1;
        native_w = src_h;
        native_h = src_w;
        for (int y = 0; y < src_h; ++y) {
            for (int x = 0; x < src_w; ++x) {
                portrait_flush_buf[x * src_h + (src_h - 1 - y)] = src[y * src_w + x];
            }
        }
        output = reinterpret_cast<const uint8_t *>(portrait_flush_buf);
    } else {
        Serial.println("Unsupported display rotation");
        lv_display_flush_ready(disp);
        return;
    }
    // Never submit coordinates outside the physical EK79007 panel.
    if (native_x < 0 || native_y < 0 ||
        native_x + native_w > 1024 || native_y + native_h > 600) {
        Serial.printf("Invalid LCD rectangle: (%d,%d) %dx%d rotation=%d\\n",
                      native_x, native_y, native_w, native_h, (int)rotation);
        lv_display_flush_ready(disp);
        return;
    }
    lcd->drawBitmap(native_x, native_y, native_w, native_h, output);
    if (lcd->getBus()->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        lv_display_flush_ready(disp);
    }
}
'''
p = p[:start] + replacement + p[end:]
needle = '    ESP_UTILS_CHECK_NULL_RETURN(lvgl_buf[1], nullptr, "Allocate LVGL buffer 1 failed");'
if needle not in p:
    raise SystemExit('Elecrow LVGL buffer initialization changed')
p = p.replace(needle, needle + '''
#if !LVGL_PORT_AVOID_TEAR
    // Allows full-screen flush rectangles without buffer overruns.
    portrait_flush_buf = static_cast<uint16_t *>(heap_caps_malloc(
        static_cast<size_t>(lcd_width) * lcd_height * 2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_UTILS_CHECK_NULL_RETURN(portrait_flush_buf, nullptr, "Allocate rotation buffer failed");
#endif''')
needle = '    if (tp->isInterruptEnabled() && xSemaphoreTake(touch_detected, 0) == pdFALSE) {\n        return;\n    }\n'
if needle not in p:
    raise SystemExit('Elecrow touch input path changed')
p = p.replace(needle, '    // Poll touch every cycle to avoid missed press/release samples.\n')
# LVGL applies display rotation to indev coordinates itself: provide native
# 1024x600 GT911 coordinates, do not rotate them a second time.
if '        data->point.x = point.x;\n        data->point.y = point.y;' not in p:
    raise SystemExit('Elecrow touch coordinates changed')
port.write_text(p)
print('Prepared portrait sketch using LVGL PARTIAL rendering + rotated MIPI flush')
