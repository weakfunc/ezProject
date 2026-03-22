import sensor, image, time, lcd
from fpioa_manager import fm
from machine import UART

# ───────────────────────────────────────────
# LCD 初始化
# ───────────────────────────────────────────
lcd.init()
lcd.rotation(1)

# ───────────────────────────────────────────
# 串口初始化（关键：必须先注册引脚！）
# 根据你的实际接线修改引脚号（7/6 或其他）
# ───────────────────────────────────────────
fm.register(7, fm.fpioa.UART1_TX, force=True)   # TX 引脚，接 STM32 RX
fm.register(6, fm.fpioa.UART1_RX, force=True)   # RX 引脚，接 STM32 TX
uart = UART(UART.UART1, 115200, 8, 1, 0, timeout=1000, read_buf_len=4096)

# ───────────────────────────────────────────
# 配置区
# ───────────────────────────────────────────
COOLDOWN_MS = 2000
MIN_PIXELS  = 500

RED    = (30, 100,  40,  80,  10,  80)
GREEN  = (30, 100, -80, -20,  10,  80)
BLUE   = ( 0,  60,   0,  40, -80, -20)
YELLOW = (50, 100, -20,  20,  30, 100)

COLOR_CFG = [
    (RED,    0x01, (255,   0,   0), "RED"   ),
    (GREEN,  0x02, (  0, 255,   0), "GREEN" ),
    (BLUE,   0x03, (  0,   0, 255), "BLUE"  ),
    (YELLOW, 0x04, (255, 255,   0), "YELLOW"),
]

# ───────────────────────────────────────────
# 摄像头初始化
# ───────────────────────────────────────────
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=2000)
sensor.set_auto_gain(False)
sensor.set_auto_whitebal(False)
sensor.set_brightness(2)
sensor.set_contrast(2)
sensor.set_saturation(2)
sensor.set_gainceiling(8)

clock = time.clock()
# ───────────────────────────────────────────
# 发送数据包
# 格式：0x55 0xAA 颜色字节 0xFF
# ───────────────────────────────────────────
def send_packet(color_byte):
    uart.write(bytes([0x55]))
    uart.write(bytes([0xAA]))
    uart.write(bytes([color_byte]))
    uart.write(bytes([0xBB]))
    print("已发送: 55 AA {:02X} FF".format(color_byte))

# ───────────────────────────────────────────
# 状态机
# ───────────────────────────────────────────
STATE_IDLE     = 0
STATE_SENT     = 1
STATE_COOLDOWN = 2

state          = STATE_IDLE
cooldown_start = 0

# ───────────────────────────────────────────
# 主循环
# ───────────────────────────────────────────
while True:
    clock.tick()
    img = sensor.snapshot()

    detected_byte  = None
    detected_label = None

    for threshold, color_byte, draw_color, label in COLOR_CFG:
        blobs = img.find_blobs(
            [threshold],
            pixels_threshold=MIN_PIXELS,
            area_threshold=MIN_PIXELS,
            merge=True
        )
        if blobs:
            b = max(blobs, key=lambda x: x.pixels())
            img.draw_rectangle(b.rect(), color=draw_color, thickness=2)
            img.draw_cross(b.cx(), b.cy(), color=draw_color, size=10)
            img.draw_string(b.x(), b.y() - 10, label, color=draw_color)
            detected_byte  = color_byte
            detected_label = label
            break

    now = time.ticks_ms()

    if state == STATE_IDLE:
        if detected_byte is not None:
            send_packet(detected_byte)
            state          = STATE_SENT
            cooldown_start = now

    elif state == STATE_SENT:
        if detected_byte is None:
            state          = STATE_COOLDOWN
            cooldown_start = now

    elif state == STATE_COOLDOWN:
        if time.ticks_diff(now, cooldown_start) >= COOLDOWN_MS:
            state = STATE_IDLE

    state_str = ["等待识别", "已发送等待消失", "冷却中"][state]
    img.draw_string(2,  2, "状态:" + state_str,                                    color=(255, 255, 255), scale=1)
    img.draw_string(2, 16, "检测:" + (detected_label if detected_label else "无"), color=(255, 255,   0), scale=1)
    img.draw_string(2, 30, "FPS:{:.1f}".format(clock.fps()),                       color=(255, 255, 255), scale=1)

    lcd.display(img)

# ───────────────────────────────────────────
# 退出时释放串口
# ───────────────────────────────────────────
uart.deinit()
del uart
