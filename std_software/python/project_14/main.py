import cv2
import ctypes
import threading
import time
import math
import serial
import serial.tools.list_ports
import struct
from collections import deque

# ── 串口选择 ──────────────────────────────────
def select_com_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("未找到任何串口设备")
        return None
    print("\n========== 可用串口 ==========")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device:<10} {p.description}")
    print("================================")
    while True:
        try:
            idx = int(input(f"请选择串口编号 (0~{len(ports)-1}): "))
            if 0 <= idx < len(ports):
                return ports[idx].device
            else:
                print("编号超出范围")
        except ValueError:
            print("请输入数字")

COM_PORT  = select_com_port()
BAUD_RATE = 115200

# ── 配置 ──────────────────────────────────────
SCREEN_W      = 1920
SCREEN_H      = 1080
CAM_W         = 1280
CAM_H         = 720
FOV_H         = 112.0
FOV_V         = 80.0
SEND_INTERVAL = 0.01
# ─────────────────────────────────────────────

DEG_PER_PX_X = FOV_H / CAM_W
DEG_PER_PX_Y = FOV_V / CAM_H

def gaze_to_angle(gx, gy):
    dx =  gx * CAM_W - CAM_W / 2
    dy =  gy * CAM_H - CAM_H / 2
    return dx * DEG_PER_PX_X, -dy * DEG_PER_PX_Y

# ── 串口初始化 ────────────────────────────────
serial_ok      = False
ser            = None
pkt_cnt        = 0
serial_log     = deque(maxlen=5)
last_send_time = 0

if COM_PORT:
    try:
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=0.1)
        print(f"串口 {COM_PORT} 已连接")
        serial_ok = True
    except Exception as e:
        print(f"串口连接失败: {e}")

def send_angle(yaw, pitch):
    global pkt_cnt, last_send_time
    if not serial_ok:
        return
    now = time.time()
    if now - last_send_time < SEND_INTERVAL:
        return
    last_send_time = now
    try:
        data   = struct.pack('<ff', yaw, pitch) + b'\x00\x00'
        header = bytes([0x55, 0xAA, 0x01]) + data
        pkt    = header + bytes([0x00, pkt_cnt & 0xFF, 0xFF])
        ser.write(pkt)
        hex_str = ' '.join(f'{b:02X}' for b in pkt)
        serial_log.append(f"#{pkt_cnt:03d} {hex_str}")
        pkt_cnt += 1
    except Exception as e:
        serial_log.append(f"ERR: {e}")

# ── 眼动仪初始化 ──────────────────────────────
dll_path = r"C:\Program Files\Tobii\Tobii EyeX\tobii_stream_engine.dll"
tobii    = ctypes.CDLL(dll_path)

class TobiiGazePoint(ctypes.Structure):
    _fields_ = [
        ("timestamp_us", ctypes.c_int64),
        ("validity",     ctypes.c_int),
        ("x",            ctypes.c_float),
        ("y",            ctypes.c_float),
    ]

GAZE_CALLBACK = ctypes.CFUNCTYPE(
    None, ctypes.POINTER(TobiiGazePoint), ctypes.c_void_p
)

gaze_x, gaze_y = 0.5, 0.5
gaze_valid      = False
gaze_lock       = threading.Lock()
running         = True

def gaze_callback(gaze_point, user_data):
    global gaze_x, gaze_y, gaze_valid
    g = gaze_point.contents
    if g.validity == 1 and not (math.isnan(g.x) or math.isnan(g.y)):
        with gaze_lock:
            gaze_x, gaze_y, gaze_valid = g.x, g.y, True
        send_angle(*gaze_to_angle(g.x, g.y))
    else:
        with gaze_lock:
            gaze_valid = False

cb = GAZE_CALLBACK(gaze_callback)
api = ctypes.c_void_p()
tobii.tobii_api_create(ctypes.byref(api), None, None)
urls      = (ctypes.c_char_p * 16)()
url_count = ctypes.c_int(0)

def url_receiver(url, user_data):
    urls[url_count.value] = url
    url_count.value += 1

URL_RECEIVER = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_void_p)
url_cb = URL_RECEIVER(url_receiver)
tobii.tobii_enumerate_local_device_urls(api, url_cb, None)
device = ctypes.c_void_p()
tobii.tobii_device_create(api, urls[0], ctypes.c_int(1), ctypes.byref(device))
tobii.tobii_gaze_point_subscribe(device, cb, None)

def gaze_thread():
    while running:
        tobii.tobii_device_process_callbacks(device)
        time.sleep(0.005)

threading.Thread(target=gaze_thread, daemon=True).start()

# ── 摄像头初始化 ──────────────────────────────
cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
cap.set(cv2.CAP_PROP_FRAME_WIDTH,  CAM_W)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAM_H)
cap.set(cv2.CAP_PROP_FPS,          30)
cap.set(cv2.CAP_PROP_BRIGHTNESS,   0)
cap.set(cv2.CAP_PROP_CONTRAST,     90)
cap.set(cv2.CAP_PROP_SATURATION,   80)
cap.set(cv2.CAP_PROP_SHARPNESS,    0)
cap.set(cv2.CAP_PROP_GAMMA,        40)

cv2.namedWindow("EyeGimbal", cv2.WINDOW_NORMAL)
cv2.resizeWindow("EyeGimbal", CAM_W, CAM_H)

fps         = 0
fps_counter = 0
fps_timer   = time.time()

# 配色：只用绿/红，标签用灰
C_GREEN = (80,  210, 80 )
C_RED   = (80,  80,  220)
C_GRAY  = (140, 148, 158)
C_WHITE = (220, 220, 220)
C_CROSS = (80,  210, 80 )

FT = cv2.FONT_HERSHEY_DUPLEX
FS = 0.42
LH = 19
LABEL_W = 55  # 标签列宽（像素），用于数据对齐

def pt(frame, text, pos, color, scale=FS):
    cv2.putText(frame, text, pos, FT, scale, color, 1, cv2.LINE_AA)

def draw_row(frame, y, label, value, val_color):
    pt(frame, label, (12, y), C_GRAY)           # 标签左对齐
    pt(frame, value, (12 + LABEL_W, y), val_color)  # 数据固定起始位置

def draw_crosshair(frame, cx, cy, size=28, gap=9):
    cv2.line(frame, (cx-size, cy), (cx-gap,  cy), C_CROSS, 1, cv2.LINE_AA)
    cv2.line(frame, (cx+gap,  cy), (cx+size, cy), C_CROSS, 1, cv2.LINE_AA)
    cv2.line(frame, (cx, cy-size), (cx, cy-gap),  C_CROSS, 1, cv2.LINE_AA)
    cv2.line(frame, (cx, cy+gap),  (cx, cy+size),  C_CROSS, 1, cv2.LINE_AA)
    cv2.circle(frame, (cx, cy), gap-2, C_CROSS, 1, cv2.LINE_AA)

def draw_frame(frame):
    global fps
    h, w = frame.shape[:2]
    cx, cy = w // 2, h // 2

    cv2.rectangle(frame, (0, 0), (w-1, h-1), C_CROSS, 1, cv2.LINE_AA)
    draw_crosshair(frame, cx, cy)

    with gaze_lock:
        gx, gy = gaze_x, gaze_y
        valid  = gaze_valid

    gaze_px    = int(gx * SCREEN_W)
    gaze_py    = int(gy * SCREEN_H)
    cam_gx     = max(0, min(w-1, int(gx * w)))
    cam_gy     = max(0, min(h-1, int(gy * h)))
    dx         = cam_gx - cx
    dy         = cam_gy - cy
    yaw, pitch = gaze_to_angle(gx, gy)

    # 注视点
    if valid:
        cv2.circle(frame, (cam_gx, cam_gy), 12, C_RED, 1, cv2.LINE_AA)
        cv2.circle(frame, (cam_gx, cam_gy),  3, C_RED, -1, cv2.LINE_AA)
        cv2.line(frame, (cx, cy), (cam_gx, cam_gy), C_CROSS, 1, cv2.LINE_AA)
        lx = cam_gx + 15 if cam_gx < w - 160 else cam_gx - 155
        pt(frame, f"({gaze_px},{gaze_py})", (lx, cam_gy - 8), C_GRAY, 0.36)

    # ── 左上角调试信息 ────────────────────────
    port_str = COM_PORT if COM_PORT else "None"

    # fps 低于20认为异常
    fps_col   = C_GREEN if fps >= 20       else C_RED
    # 串口状态
    ser_col   = C_GREEN if serial_ok       else C_RED
    ser_val   = f"{port_str}  OK" if serial_ok else f"{port_str}  FAIL"
    # 眼动仪状态
    eye_col   = C_GREEN if valid           else C_RED
    eye_val   = "LOCKED"          if valid else "LOST"
    # 角度正常范围 ±50°
    yaw_col   = C_GREEN if abs(yaw)   < 50 else C_RED
    pitch_col = C_GREEN if abs(pitch) < 35 else C_RED

    rows = [
        ("FPS",   f"{fps}",                         fps_col  ),
        ("GAZE",  f"({gaze_px:>5}, {gaze_py:>5})",  C_GREEN  ),
        ("DX",    f"{dx:>+6} px",                   C_GREEN  ),
        ("DY",    f"{dy:>+6} px",                   C_GREEN  ),
        ("YAW",   f"{yaw:>+8.2f} deg",              yaw_col  ),
        ("PITCH", f"{pitch:>+8.2f} deg",            pitch_col),
        ("PORT",  ser_val,                           ser_col  ),
        ("EYE",   eye_val,                           eye_col  ),
    ]

    for i, (label, value, col) in enumerate(rows):
        draw_row(frame, 18 + i * LH, label, value, col)

    # ── 左下角串口日志 ────────────────────────
    logs   = list(serial_log)
    base_y = h - 14 - len(logs) * 16
    pt(frame, "SERIAL TX", (12, base_y - 4), C_GRAY, 0.36)
    for i, line in enumerate(logs):
        pt(frame, line, (12, base_y + 12 + i * 16), C_WHITE, 0.34)

print("按 Q 退出")

while True:
    ret, frame = cap.read()
    if not ret:
        break

    fps_counter += 1
    if time.time() - fps_timer >= 1.0:
        fps         = fps_counter
        fps_counter = 0
        fps_timer   = time.time()

    draw_frame(frame)
    cv2.imshow("EyeGimbal", frame)

    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

running = False
tobii.tobii_gaze_point_unsubscribe(device)
tobii.tobii_device_destroy(device)
tobii.tobii_api_destroy(api)
cap.release()
if ser and serial_ok:
    ser.close()
cv2.destroyAllWindows()