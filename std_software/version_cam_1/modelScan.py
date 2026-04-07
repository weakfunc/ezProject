from maix import camera, display, image, nn, app, uart, time
import os

device  = "/dev/ttyS0"
serial0 = uart.UART(device, 115200)

COOLDOWN_MS  = 2000
ENABLE_CRC   = True
STABLE_FRAMES = 3

# 中文标签 → 英文显示 + 协议编码
LABEL_MAP = {
    "圆形": ("Circle", 0x01),
    "矩形": ("Rect",   0x02),
}

def get_label_info(label):
    return LABEL_MAP.get(label, (label, 0x00))

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
#   [3]     shape_code  0x01=圆形 0x02=矩形
#   [4~12]  0x00        保留9字节
#   [13]    CRC8
#   [14]    CNT
#   [15]    0xFF
# ───────────────────────────────────────────
pkt_cnt = 0

def send_packet(shape_code, shape_name):
    global pkt_cnt
    data    = bytes([shape_code]) + bytes(9)
    pre     = bytes([0x55, 0xAA, 0x01]) + data
    crc     = crc8(pre) if ENABLE_CRC else 0x00
    cnt     = pkt_cnt & 0xFF
    pkt_cnt += 1
    pkt     = pre + bytes([crc, cnt, 0xFF])
    serial0.write(pkt)
    hex_str = " ".join("{:02X}".format(b) for b in pkt)
    print("=" * 40)
    print("已发送数据包")
    print("HEX: " + hex_str)
    print("Shape: {} (0x{:02X})".format(shape_name, shape_code))
    print("CRC: {:02X} ({})  CNT: {:d}".format(
        crc,
        "校验值" if ENABLE_CRC else "已禁用",
        cnt
    ))
    print("=" * 40)

# ───────────────────────────────────────────
# 模型加载
# ───────────────────────────────────────────
model_path = "userModel.mud"
if not os.path.exists(model_path):
    model_path = "/root/models/userModel.mud"
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
last_shape     = None   # (name, code)
stable_count   = 0
stable_shape   = None   # (name, code)

# ───────────────────────────────────────────
# 主循环
# ───────────────────────────────────────────
while not app.need_exit():
    img  = cam.read()
    objs = detector.detect(img, conf_th=0.5, iou_th=0.45)

    # 取置信度最高的目标
    detected_shape = None
    best_obj       = None
    for obj in objs:
        if best_obj is None or obj.score > best_obj.score:
            best_obj = obj

    if best_obj is not None:
        label_en, shape_code = get_label_info(detector.labels[best_obj.class_id])
        if shape_code != 0x00:
            detected_shape = (label_en, shape_code)
        # 画框
        img.draw_rect(best_obj.x, best_obj.y, best_obj.w, best_obj.h, color=image.COLOR_RED)
        msg = '{}: {:.2f}'.format(label_en, best_obj.score)
        img.draw_string(best_obj.x, best_obj.y, msg, color=image.COLOR_RED)

    now = time.ticks_ms()

    if state == STATE_IDLE:
        if detected_shape is not None:
            stable_shape = detected_shape
            stable_count = 1
            state        = STATE_STABLE
            print("开始稳定帧累积: {}".format(detected_shape[0]))

    elif state == STATE_STABLE:
        if detected_shape is None:
            stable_count = 0
            stable_shape = None
            state        = STATE_IDLE
            print("目标消失，重置")
        elif detected_shape[1] == stable_shape[1]:
            stable_count += 1
            print("稳定帧 {}/{}".format(stable_count, STABLE_FRAMES))
            if stable_count >= STABLE_FRAMES:
                send_packet(stable_shape[1], stable_shape[0])
                last_shape     = stable_shape
                state          = STATE_SENT
                cooldown_start = now
        else:
            stable_shape = detected_shape
            stable_count = 1
            print("形状跳变，重新累积: {}".format(detected_shape[0]))

    elif state == STATE_SENT:
        if detected_shape is None:
            state          = STATE_COOLDOWN
            cooldown_start = now

    elif state == STATE_COOLDOWN:
        if now - cooldown_start >= COOLDOWN_MS:
            state        = STATE_IDLE
            last_shape   = None
            stable_count = 0
            stable_shape = None
            print("COOLDOWN结束 -> IDLE")

    # ── 屏幕显示 ──
    if detected_shape is not None:
        img.draw_string(0,  0, "Scan: " + detected_shape[0], image.COLOR_BLUE)
    else:
        img.draw_string(0,  0, "Scan: None",                 image.COLOR_WHITE)

    state_str   = ["IDLE", "STABLE", "SENT-WAIT", "COOLDOWN"][state]
    state_color = [image.COLOR_WHITE, image.COLOR_BLUE, image.COLOR_GREEN, image.COLOR_YELLOW][state]
    img.draw_string(0, 20, "State: " + state_str, state_color)

    if state == STATE_STABLE:
        img.draw_string(0, 40, "Stab: {}/{}".format(stable_count, STABLE_FRAMES), image.COLOR_BLUE)
    elif state in (STATE_SENT, STATE_COOLDOWN) and last_shape:
        img.draw_string(0, 40, "Sent: " + last_shape[0], image.COLOR_GREEN)
    else:
        img.draw_string(0, 40, "Sent: --",                image.COLOR_WHITE)

    if state == STATE_COOLDOWN:
        remaining = max(0, COOLDOWN_MS - (now - cooldown_start))
        img.draw_string(0, 60, "CD: {:.1f}s".format(remaining / 1000.0), image.COLOR_YELLOW)
    else:
        img.draw_string(0, 60, "CD: --", image.COLOR_WHITE)

    dis.show(img)
    time.sleep_ms(1)