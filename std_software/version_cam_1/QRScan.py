from maix import camera, display, image, app, uart, time

device  = "/dev/ttyS0"
serial0 = uart.UART(device, 115200)

COOLDOWN_MS  = 2000
DATA_MAX_LEN = 10

# ───────────────────────────────────────────
# 功能开关
# ───────────────────────────────────────────
ENABLE_CRC   = True   # CRC8校验开关：True=开启，False=关闭（CRC字段发0x00）

cam  = camera.Camera(320, 240)
disp = display.Display()

# ───────────────────────────────────────────
# CRC8 校验计算（多项式 0x07）
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
#   [0]     0x55        包头1
#   [1]     0xAA        包头2
#   [2]     0x01        控制字段
#   [3~12]  data        数据内容（10字节，不足补0x00）
#   [13]    CRC8        ENABLE_CRC=True时为校验值，False时为0x00
#   [14]    CNT         包计数（0x00~0xFF 循环递增）
#   [15]    0xFF        包尾
# ───────────────────────────────────────────
pkt_cnt = 0

def send_packet(payload_str):
    global pkt_cnt

    data    = payload_str.encode('utf-8')[:DATA_MAX_LEN]
    data    = data + bytes(DATA_MAX_LEN - len(data))
    pre     = bytes([0x55, 0xAA, 0x01]) + data   # [0~12] 共13字节

    # ⭐ CRC字段：根据开关决定计算还是填0x00
    crc     = crc8(pre) if ENABLE_CRC else 0x00

    cnt     = pkt_cnt & 0xFF
    pkt_cnt += 1

    pkt     = pre + bytes([crc, cnt, 0xFF])

    serial0.write(pkt)

    hex_str = " ".join("{:02X}".format(b) for b in pkt)
    print("=" * 40)
    print("已发送数据包")
    print("HEX: " + hex_str)
    print("QR:  " + payload_str)
    print("CRC: {:02X} ({})  CNT: {:d}".format(
        crc,
        "校验值" if ENABLE_CRC else "已禁用",
        cnt
    ))
    print("=" * 40)

# ───────────────────────────────────────────
# 状态机
# ───────────────────────────────────────────
STATE_IDLE     = 0
STATE_SENT     = 1
STATE_COOLDOWN = 2

state          = STATE_IDLE
cooldown_start = 0
last_payload   = ""

# ───────────────────────────────────────────
# 主循环
# ───────────────────────────────────────────
while not app.need_exit():
    img = cam.read()

    detected_payload = None
    qrcodes = img.find_qrcodes()
    for q in qrcodes:
        detected_payload = q.payload()
        corners = q.corners()
        for i in range(4):
            img.draw_line(
                corners[i][0],       corners[i][1],
                corners[(i+1)%4][0], corners[(i+1)%4][1],
                image.COLOR_RED
            )
        break

    now = time.ticks_ms()

    if state == STATE_IDLE:
        if detected_payload is not None:
            send_packet(detected_payload)
            last_payload   = detected_payload
            state          = STATE_SENT
            cooldown_start = now

    elif state == STATE_SENT:
        if detected_payload is None:
            state          = STATE_COOLDOWN
            cooldown_start = now

    elif state == STATE_COOLDOWN:
        elapsed = now - cooldown_start
        if elapsed >= COOLDOWN_MS:
            state        = STATE_IDLE
            last_payload = ""
            print("COOLDOWN结束 -> IDLE")

    if detected_payload is not None:
        img.draw_string(0,  0, "Scan: " + detected_payload[:10], image.COLOR_BLUE)
    else:
        img.draw_string(0,  0, "Scan: None",                     image.COLOR_WHITE)

    state_str   = ["IDLE", "SENT-WAIT", "COOLDOWN"][state]
    state_color = [image.COLOR_WHITE, image.COLOR_GREEN, image.COLOR_YELLOW][state]
    img.draw_string(0, 20, "State: " + state_str,                state_color)

    if state in (STATE_SENT, STATE_COOLDOWN) and last_payload:
        img.draw_string(0, 40, "Sent: " + last_payload[:10],     image.COLOR_GREEN)
    else:
        img.draw_string(0, 40, "Sent: --",                       image.COLOR_WHITE)

    if state == STATE_COOLDOWN:
        elapsed   = now - cooldown_start
        remaining = max(0, COOLDOWN_MS - elapsed)
        img.draw_string(0, 60, "CD: {:.1f}s".format(remaining / 1000.0), image.COLOR_YELLOW)
    else:
        img.draw_string(0, 60, "CD: --",                         image.COLOR_WHITE)

    disp.show(img)
    time.sleep_ms(1)