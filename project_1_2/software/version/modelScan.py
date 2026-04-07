from maix import camera, display, image, nn, app, uart, time
import os

device  = "/dev/ttyS0"
serial0 = uart.UART(device, 115200)

COOLDOWN_MS   = 2500
ENABLE_CRC    = False
STABLE_FRAMES = 5

# 形状映射
LABEL_MAP = {
    "圆形": ("Circle", 0x01),
    "矩形": ("Rect",   0x00),
}

def get_label_info(label):
    return LABEL_MAP.get(label, (label, 0xFF))

# ───────────────────────────────────────────
# 颜色识别（LAB色彩空间）
# 返回 (颜色名, 颜色code)
# ───────────────────────────────────────────
RED_T   = (30,  80,  40, 127, -20,  60)
WHITE_T = (70, 100, -20,  20, -20,  20)
BLACK_T = ( 0,  50, -30,  30, -30,  30)

COLOR_LIST = [
    ("White", 0x01, WHITE_T),
    ("Red",   0x02, RED_T),
    ("Black", 0x03, BLACK_T),
]

def detect_color(img, obj):
    """在目标bbox中心区域采样，判断颜色"""
    # 取bbox中心60%区域避免边缘干扰
    cx = obj.x + obj.w // 2
    cy = obj.y + obj.h // 2
    rw = max(10, obj.w * 6 // 10)
    rh = max(10, obj.h * 6 // 10)
    roi = (cx - rw // 2, cy - rh // 2, rw, rh)

    best_color  = ("Unknown", 0xFF)
    best_pixels = 0

    for name, code, thresh in COLOR_LIST:
        blobs = img.find_blobs([thresh], roi=roi, pixels_threshold=10, area_threshold=10)
        total = sum(b.pixels() for b in blobs)
        if total > best_pixels:
            best_pixels = total
            best_color  = (name, code)

    return best_color

# ───────────────────────────────────────────
# CRC8
# ───────────────────────────────────────────
def crc8(data):
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ 0x07
            else:
                crc <<= 1
            crc &= 0xFF
    return crc

# ───────────────────────────────────────────
# 数据包格式（固定16字节）：
#   [0]     0x55
#   [1]     0xAA
#   [2]     0x01        控制字段
#   [3]     color_code  白0x00 红0x01 黑0x03
#   [4]     0x00        保留
#   [5]     0x00        保留
#   [6]     0x00        保留
#   [7]     shape_code  矩形0x00 圆形0x01
#   [8~12]  0x00        保留5字节
#   [13]    CRC8
#   [14]    CNT
#   [15]    0xFF
# ───────────────────────────────────────────
pkt_cnt = 0

def send_packet(color_code, color_name, shape_code, shape_name):
    global pkt_cnt
    data    = bytes([color_code, 0x00, 0x00, 0x00, shape_code]) + bytes(5)  # [3]color [4~6]0x00 [7]shape [8~12]0x00
    pre     = bytes([0x55, 0xAA, 0x01]) + data                               # 这行之前漏掉了
    crc     = crc8(pre) if ENABLE_CRC else 0x00
    cnt     = pkt_cnt & 0xFF
    pkt_cnt += 1
    pkt     = pre + bytes([crc, cnt, 0xFF])
    serial0.write(pkt)
    hex_str = " ".join("{:02X}".format(b) for b in pkt)
    print("=" * 40)
    print("已发送数据包")
    print("HEX: " + hex_str)
    print("Color: {} (0x{:02X})  Shape: {} (0x{:02X})".format(
        color_name, color_code, shape_name, shape_code))
    print("CRC: {:02X} ({})  CNT: {:d}".format(
        crc, "校验值" if ENABLE_CRC else "已禁用", cnt))
    print("=" * 40)

# ───────────────────────────────────────────
# 模型加载
# ───────────────────────────────────────────
model_path = "model_260857.mud"
if not os.path.exists(model_path):
    model_path = "/root/models/model_260857.mud"
detector = nn.YOLOv5(model=model_path)

cam = camera.Camera(detector.input_width(), detector.input_height(), detector.input_format())
dis = display.Display()

# ───────────────────────────────────────────
# 状态机
# ───────────────────────────────────────────
STATE_IDLE     = 0
STATE_STABLE   = 1
STATE_SENT     = 2
STATE_COOLDOWN = 3

state          = STATE_IDLE
cooldown_start = 0
last_result    = None   # (color_name, color_code, shape_name, shape_code)
stable_count   = 0
stable_result  = None

while not app.need_exit():
    img  = cam.read()
    objs = detector.detect(img, conf_th=0.15, iou_th=0.45)

    # 取置信度最高目标
    detected_result = None
    best_obj        = None
    for obj in objs:
        if best_obj is None or obj.score > best_obj.score:
            best_obj = obj

    if best_obj is not None:
        label_en, shape_code = get_label_info(detector.labels[best_obj.class_id])
        color_name, color_code = detect_color(img, best_obj)

        if shape_code != 0xFF:
            detected_result = (color_name, color_code, label_en, shape_code)

        # 画框和标签
        img.draw_rect(best_obj.x, best_obj.y, best_obj.w, best_obj.h, color=image.COLOR_RED)
        msg = '{} {}: {:.2f}'.format(color_name, label_en, best_obj.score)
        img.draw_string(best_obj.x, best_obj.y, msg, color=image.COLOR_RED)

    now = time.ticks_ms()

    if state == STATE_IDLE:
        if detected_result is not None:
            stable_result = detected_result
            stable_count  = 1
            state         = STATE_STABLE
            print("开始稳定帧累积: {} {}".format(detected_result[0], detected_result[2]))

    elif state == STATE_STABLE:
        if detected_result is None:
            stable_count  = 0
            stable_result = None
            state         = STATE_IDLE
            print("目标消失，重置")
        elif (detected_result[1] == stable_result[1] and
              detected_result[3] == stable_result[3]):
            stable_count += 1
            print("稳定帧 {}/{}".format(stable_count, STABLE_FRAMES))
            if stable_count >= STABLE_FRAMES:
                send_packet(
                    stable_result[1], stable_result[0],
                    stable_result[3], stable_result[2]
                )
                last_result    = stable_result
                state          = STATE_SENT
                cooldown_start = now
        else:
            stable_result = detected_result
            stable_count  = 1
            print("结果跳变，重新累积: {} {}".format(detected_result[0], detected_result[2]))

    elif state == STATE_SENT:
        if detected_result is None:
            state          = STATE_COOLDOWN
            cooldown_start = now

    elif state == STATE_COOLDOWN:
        if now - cooldown_start >= COOLDOWN_MS:
            state         = STATE_IDLE
            last_result   = None
            stable_count  = 0
            stable_result = None
            print("COOLDOWN结束 -> IDLE")

    # ── 屏幕左上角显示 ──
    if detected_result is not None:
        color_show = detected_result[0]
        shape_show = detected_result[2]
        img.draw_string(0,  0, "Color: " + color_show, image.COLOR_BLUE)
        img.draw_string(0, 20, "Shape: " + shape_show, image.COLOR_BLUE)
    else:
        img.draw_string(0,  0, "Color: --",            image.COLOR_WHITE)
        img.draw_string(0, 20, "Shape: --",            image.COLOR_WHITE)

    state_str   = ["IDLE", "STABLE", "SENT-WAIT", "COOLDOWN"][state]
    state_color = [image.COLOR_WHITE, image.COLOR_BLUE, image.COLOR_GREEN, image.COLOR_YELLOW][state]
    img.draw_string(0, 40, "State: " + state_str, state_color)

    if state == STATE_STABLE:
        img.draw_string(0, 60, "Stab: {}/{}".format(stable_count, STABLE_FRAMES), image.COLOR_BLUE)
    elif state in (STATE_SENT, STATE_COOLDOWN) and last_result:
        img.draw_string(0, 60, "Sent: {} {}".format(last_result[0], last_result[2]), image.COLOR_GREEN)
    else:
        img.draw_string(0, 60, "Sent: --", image.COLOR_WHITE)

    if state == STATE_COOLDOWN:
        remaining = max(0, COOLDOWN_MS - (now - cooldown_start))
        img.draw_string(0, 80, "CD: {:.1f}s".format(remaining / 1000.0), image.COLOR_YELLOW)
    else:
        img.draw_string(0, 80, "CD: --", image.COLOR_WHITE)

    dis.show(img)
    time.sleep_ms(1)