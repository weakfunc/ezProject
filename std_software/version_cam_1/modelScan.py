from maix import camera, display, image, nn, app, uart, time
import os

device  = "/dev/ttyS0"
serial0 = uart.UART(device, 115200)

ENABLE_CRC     = False
USE_YOLO       = False   # True=YOLOv5识别  False=用户自定义识别
SEND_PERIOD_MS = 10
COOLDOWN_MS    = 2000
UDET_SKIP      = 0   # 每 UDET_SKIP+1 帧执行一次扫描，其余帧返回缓存

# ───────────────────────────────────────────
# 用户配置
# 中文标签与训练标签一一对应
# ───────────────────────────────────────────
LABEL_MAP = {
    "圆形": ("Circle", 0x03),
    "矩形": ("Rect",   0x02),
}



model_path = "model_260857.mud"
if not os.path.exists(model_path):
    model_path = "/root/models/model_260857.mud"

def get_label_info(label):
    return LABEL_MAP.get(label, (label, 0xFF))

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
#   [3]     obj1        分类1编码（无目标/冷却中=0xFF）
#   [4]     obj2        分类2编码（无目标/冷却中=0xFF）
#   [5]     fps         摄像头帧率（uint8）
#   [6~12]  0x00        保留
#   [13]    CRC8
#   [14]    CNT
#   [15]    0xFF
# ───────────────────────────────────────────
pkt_cnt = 0

def send_packet(obj1, obj2, fps):
    global pkt_cnt
    fps_byte = min(int(fps), 255)
    data     = bytes([obj1, obj2, fps_byte]) + bytes(7)
    pre      = bytes([0x55, 0xAA, 0x01]) + data
    crc      = crc8(pre) if ENABLE_CRC else 0x00
    cnt      = pkt_cnt & 0xFF
    pkt_cnt += 1
    pkt      = pre + bytes([crc, cnt, 0xFF])
    serial0.write(pkt)
    return pkt, cnt

# ───────────────────────────────────────────
# 模型加载 & 摄像头初始化
# ───────────────────────────────────────────
if USE_YOLO:
    detector = nn.YOLOv5(model=model_path)
    cam = camera.Camera(detector.input_width(), detector.input_height(), detector.input_format())
else:
    cam = camera.Camera(320, 240)
dis = display.Display()

# ───────────────────────────────────────────
# 用户自定义识别函数（USE_YOLO=False 时生效）
# 返回 (has_target: bool, obj1: int, obj2: int)
# ───────────────────────────────────────────
_udet_frame   = 0
_udet_cache   = (False, 0xFF, 0xFF)
_udet_corners = []   # 最近一次检测到的QR角点，供主循环画框

def user_detect(img):
    global _udet_frame, _udet_cache, _udet_corners
    _udet_frame += 1
    if _udet_frame % (UDET_SKIP + 1) != 0:
        return _udet_cache
    qrcodes = img.find_qrcodes()
    for q in qrcodes:
        b             = q.payload().encode('utf-8')
        obj1          = b[0] if len(b) > 0 else 0xFF
        obj2          = b[1] if len(b) > 1 else 0xFF
        _udet_corners = q.corners()
        _udet_cache   = (True, obj1, obj2)
        return _udet_cache
    _udet_corners = []
    _udet_cache   = (False, 0xFF, 0xFF)
    return _udet_cache

# ───────────────────────────────────────────
# 状态机
# ───────────────────────────────────────────
STATE_IDLE     = 0
STATE_COOLDOWN = 1   # 发完一帧后冷却计时中

state          = STATE_IDLE
cooldown_start = 0
last_obj1      = 0xFF
last_obj2      = 0xFF

# ───────────────────────────────────────────
# 帧率 & 发送计时
# ───────────────────────────────────────────
fps_count        = 0
fps_value        = 0
fps_window_start = time.ticks_ms()
last_send_ms     = time.ticks_ms()
null_pkt_count   = 0

# ───────────────────────────────────────────
# 主循环
# ───────────────────────────────────────────
while not app.need_exit():
    img = cam.read()

    # ── 帧率统计 ──
    fps_count += 1
    now = time.ticks_ms()
    if now - fps_window_start >= 1000:
        fps_value        = fps_count
        fps_count        = 0
        fps_window_start = now
        print("[NULL]  count={:d}  FPS={:d}".format(null_pkt_count, fps_value))

    # ── 目标识别 ──
    if USE_YOLO:
        objs = detector.detect(img, conf_th=0.5, iou_th=0.45)
        valid_objs = []
        for obj in objs:
            label_en, code = get_label_info(detector.labels[obj.class_id])
            if code != 0xFF:
                valid_objs.append((obj, label_en, code))
        valid_objs.sort(key=lambda x: x[0].score, reverse=True)
        detected_obj1 = valid_objs[0][2] if len(valid_objs) > 0 else 0xFF
        detected_obj2 = valid_objs[1][2] if len(valid_objs) > 1 else 0xFF
        has_target    = detected_obj1 != 0xFF
    else:
        valid_objs = []
        has_target, detected_obj1, detected_obj2 = user_detect(img)

    # ── 状态机 ──
    if state == STATE_IDLE:
        if has_target:
            last_obj1 = detected_obj1
            last_obj2 = detected_obj2
            pkt, cnt = send_packet(last_obj1, last_obj2, fps_value)
            last_send_ms   = now
            state          = STATE_COOLDOWN
            cooldown_start = now
            print("[VALID] HEX: {}  OBJ1:{:02X}  OBJ2:{:02X}  FPS:{:d}  CNT:{:d}".format(
                " ".join("{:02X}".format(b) for b in pkt),
                last_obj1, last_obj2, fps_value, cnt))

    elif state == STATE_COOLDOWN:
        if now - cooldown_start >= COOLDOWN_MS:
            state     = STATE_IDLE
            last_obj1 = 0xFF
            last_obj2 = 0xFF
            print("COOLDOWN结束 -> IDLE")

    # ── 周期发送（冷却/空闲期间持续发0xFF）──
    if now - last_send_ms >= SEND_PERIOD_MS:
        send_packet(0xFF, 0xFF, fps_value)
        last_send_ms    = now
        null_pkt_count += 1

    # ── 画框 ──
    if USE_YOLO:
        for obj, label_en, code in valid_objs:
            img.draw_rect(obj.x, obj.y, obj.w, obj.h, color=image.COLOR_RED)
            img.draw_string(obj.x, obj.y,
                            '{} {:.2f}'.format(label_en, obj.score),
                            color=image.COLOR_RED)
    else:
        if _udet_corners:
            for i in range(4):
                img.draw_line(
                    _udet_corners[i][0],       _udet_corners[i][1],
                    _udet_corners[(i+1)%4][0], _udet_corners[(i+1)%4][1],
                    image.COLOR_RED)

    # ── 屏幕显示 ──
    obj1_str  = "0x{:02X}".format(detected_obj1) if detected_obj1 != 0xFF else "None"
    obj2_str  = "0x{:02X}".format(detected_obj2) if detected_obj2 != 0xFF else "None"
    state_str = ["IDLE", "COOLDOWN"][state]
    state_color = [image.COLOR_WHITE, image.COLOR_YELLOW][state]

    img.draw_string(0,  0, "OBJ1: " + obj1_str,            image.COLOR_BLUE)
    img.draw_string(0, 20, "OBJ2: " + obj2_str,            image.COLOR_WHITE)
    img.draw_string(0, 40, "FPS:  {:d}".format(fps_value), image.COLOR_WHITE)
    img.draw_string(0, 60, "State: " + state_str,          state_color)

    if state == STATE_COOLDOWN:
        remaining = max(0, COOLDOWN_MS - (now - cooldown_start))
        img.draw_string(0, 80, "CD: {:.1f}s".format(remaining / 1000.0), image.COLOR_YELLOW)
    else:
        img.draw_string(0, 80, "CD: --",                   image.COLOR_WHITE)

    dis.show(img)