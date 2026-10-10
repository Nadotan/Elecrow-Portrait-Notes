from pathlib import Path
import shutil

root = Path(__file__).resolve().parent
vendor = root / 'vendor/example/V1.2/Arduino_Code/Lesson09-LVGL_Lighting_Control'
if not vendor.is_dir():
    raise SystemExit('Elecrow V1.2 sources missing; build via GitHub Actions.')

sketch = root / 'Sketch/Sketch'
sketch.mkdir(parents=True, exist_ok=True)
for source in vendor.iterdir():
    if source.is_file():
        shutil.copy2(source, sketch / source.name)

ino = sketch / 'Lesson09-LVGL_Lighting_Control.ino'
s = ino.read_text()
a = s.index('/* Button callback function - turn on LED */')
b = s.index('/*---------------------------------------------------------------\n * Arduino entry points', a)
s = s[:a] + s[b:]
s = s.replace('#include "lvgl_port.h"', '#include "lvgl_port.h"\n#include "NotesUI.h"')
if '    create_led_control_ui();' not in s:
    raise SystemExit('Elecrow UI startup path changed')
s = s.replace('    create_led_control_ui();', '    Notes::start();')
(sketch / 'Sketch.ino').write_text(s)
ino.unlink()
shutil.copy2(root / 'NotesUI.h', sketch / 'NotesUI.h')
shutil.copy2(root / 'partitions.csv', sketch / 'partitions.csv')

header = sketch / 'lvgl_port.h'
h = header.read_text()
old_mode = '#define LVGL_PORT_AVOID_TEARING_MODE            (3)'
if old_mode not in h:
    raise SystemExit('Elecrow rendering mode configuration changed')
h = h.replace(old_mode, '#define LVGL_PORT_AVOID_TEARING_MODE            (0)')
header.write_text(h)

port = sketch / 'lvgl_port.cpp'
p = port.read_text()
start = p.index('#else\nstatic void flush_callback(')
end = p.index('\nIRAM_ATTR static bool on_draw_bitmap_finish_callback', start)

replacement = r'''#else
// Preserve the image orientation that already displayed upright on the device.
// Portrait pixels are written as native_x=1023-portrait_y,
// native_y=portrait_x. GT911 touch will be pre-flipped by both native axes
// so that LVGL9's built-in 90-degree pointer rotation matches these pixels.
// The LCD remains in native 1024x600 configuration.
// PSRAM buffer stays intact until MIPI flush completion callback.
static uint16_t *portrait_flush_buf = nullptr;

static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    LCD *lcd = static_cast<LCD *>(lv_display_get_user_data(disp));
    const int src_w = area->x2 - area->x1 + 1;
    const int src_h = area->y2 - area->y1 + 1;
    const lv_display_rotation_t rotation = lv_display_get_rotation(disp);

    int native_x, native_y, native_w, native_h;
    const uint8_t *output = px_map;
    if (rotation == LV_DISPLAY_ROTATION_0) {
        native_x = area->x1;
        native_y = area->y1;
        native_w = src_w;
        native_h = src_h;
    } else if (rotation == LV_DISPLAY_ROTATION_90) {
        // Same portrait image orientation as the previously working firmware.
        native_x = 1023 - area->y2;
        native_y = area->x1;
        native_w = src_h;
        native_h = src_w;
        const uint16_t *source = reinterpret_cast<const uint16_t *>(px_map);
        for (int y = 0; y < src_h; ++y) {
            for (int x = 0; x < src_w; ++x) {
                // Rotate into row-major native physical pixels.
                portrait_flush_buf[x * src_h + (src_h - 1 - y)] =
                    source[y * src_w + x];
            }
        }
        output = reinterpret_cast<const uint8_t *>(portrait_flush_buf);
    } else {
        Serial.println("Notes V2: unsupported LCD rotation");
        lv_display_flush_ready(disp);
        return;
    }

    if (native_x < 0 || native_y < 0 || native_w <= 0 || native_h <= 0 ||
        native_x + native_w > 1024 || native_y + native_h > 600) {
        Serial.printf("Notes V2: bad flush rect x=%d y=%d w=%d h=%d rot=%d\n",
                      native_x, native_y, native_w, native_h, (int)rotation);
        lv_display_flush_ready(disp);
        return;
    }

    lcd->drawBitmap(native_x, native_y, native_w, native_h, output);
    // The MIPI-DSI path completes from on_draw_bitmap_finish_callback().
    if (lcd->getBus()->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        lv_display_flush_ready(disp);
    }
}
'''
p = p[:start] + replacement + p[end:]

needle = '    ESP_UTILS_CHECK_NULL_RETURN(lvgl_buf[1], nullptr, "Allocate LVGL buffer 1 failed");'
if needle not in p:
    raise SystemExit('Elecrow LVGL buffer configuration changed')
p = p.replace(needle, needle + '''
#if !LVGL_PORT_AVOID_TEAR
    portrait_flush_buf = static_cast<uint16_t *>(heap_caps_malloc(
        static_cast<size_t>(lcd_width) * lcd_height * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_UTILS_CHECK_NULL_RETURN(portrait_flush_buf, nullptr, "Portrait flush allocation failed");
#endif''')

# Without polling, a missed GT911 interrupt can lose press and release events.
interrupt_gate = '''    if (tp->isInterruptEnabled() && xSemaphoreTake(touch_detected, 0) == pdFALSE) {
        return;
    }
'''
if interrupt_gate not in p:
    raise SystemExit('Vendor GT911 touch event code changed')
p = p.replace(interrupt_gate, '    // Poll on each LVGL read cycle to avoid lost press/release events.\n')
# LVGL9.1's input rotation 90 is (599-in_y, in_x).
# Flip both raw native axes so the final displayed touch position is
# (native_y,1023-native_x), matching the already-upright output bitmap.
raw_point = '''        data->point.x = point.x;
        data->point.y = point.y;'''
if raw_point not in p:
    raise SystemExit('Vendor GT911 coordinate code changed')
p = p.replace(raw_point, '''        data->point.x = 1023 - point.x;
        data->point.y = 599 - point.y;''')
port.write_text(p)
print('Prepared Notes V2 / matching LVGL9 rotation, portrait canvas, and 16MB SPIFFS layout')
