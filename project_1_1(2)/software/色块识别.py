import sensor, image, time, lcd
from fpioa_manager import fm
from machine import UART

# ───────────────────────────────────────────
# LCD 初始化
# ───────────────────────────────────────────
lcd.init()
lcd.rotation(1)

# ───────────────────────────────────────────
# 串口初始化
# ───────────────────────────────────────────
fm.register(7, fm.fpioa.UART1_TX, force=True)
fm.register(6, fm.fpioa.UART1_RX, force=True)
uart = UART(UART.UART1, 115200, 8, 1, 0, timeout=1000, read_buf_len=4096)

# ───────────────────────────────────────────
# 配置区
# ───────────────────────────────────────────
COOLDOWN_MS = 2000
MIN_PIXELS  = 500

YELLOW = (55, 100, -15,  25,  35, 100)
GREEN  = (20,  80, -80, -30,  10,  70)
RED    = (30, 100,  40,  80,  10,  80)

COLOR_CFG = [
    (RED,    0x31, (255,   0,   0), "RED"   ),
    (YELLOW, 0x32, (255, 255,   0), "YELLOW"),
    (GREEN,  0x33, (  0, 255,   0), "GREEN" ),
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
# 发送16字节固定帧
# ───────────────────────────────────────────
last_tx_str = "无"

def send_packet(color_byte):
    global last_tx_str
    pkt = bytearray(16)
    pkt[0]  = 0x55
    pkt[1]  = 0xAA
    pkt[2]  = 0x01
    pkt[3]  = color_byte
    pkt[13] = 0x00
    pkt[14] = 0x00
    pkt[15] = 0xFF
    uart.write(pkt)
    last_tx_str = " ".join("{:02X}".format(b) for b in pkt)
    print("TX:", last_tx_str)

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

    # ─── 屏幕信息显示 ───
    if state == STATE_IDLE:
        tx_status = "未发送"
        tx_color  = (255, 255, 255)
    elif state == STATE_SENT:
        tx_status = "已发送!"
        tx_color  = (0, 255, 0)
    else:
        tx_status = "冷却中"
        tx_color  = (255, 165, 0)

    if state == STATE_COOLDOWN:
        remain = COOLDOWN_MS - time.ticks_diff(now, cooldown_start)
        if remain < 0:
            remain = 0
        cd_str = "{:.1f}s".format(remain / 1000.0)
    elif state == STATE_SENT:
        cd_str = "等待消失"
    else:
        cd_str = "就绪"

    state_str = ["等待识别", "已发送等待消失", "冷却中"][state]

    y = 2
    img.draw_string(2, y, "状态:" + state_str,                                     color=(255, 255, 255), scale=1); y += 14
    img.draw_string(2, y, "检测:" + (detected_label if detected_label else "无"),  color=(255, 255,   0), scale=1); y += 14
    img.draw_string(2, y, "串口:" + tx_status,                                     color=tx_color,        scale=1); y += 14
    img.draw_string(2, y, "冷却:" + cd_str,                                        color=(255, 165,   0), scale=1); y += 14
    img.draw_string(2, y, "FPS:{:.1f}".format(clock.fps()),                        color=(255, 255, 255), scale=1); y += 14
    img.draw_string(2, y, "TX:" + last_tx_str[:35],                                color=(0, 255, 255),   scale=1)

    # LCD单独翻转显示，IDE保持原始方向
    lcd.display(img.copy().replace(hmirror=True))

uart.deinit()
del uart
