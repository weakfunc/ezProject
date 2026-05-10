import cv2
import ctypes
import os
import string
import sys
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
TX_INTERVAL   = 0.01   # 10 ms 固定发送周期
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

# ── STM32 反馈 ────────────────────────────────
stm32_yaw      = 0.0
stm32_pitch    = 0.0
stm32_fb_valid = False
stm32_lock     = threading.Lock()

# ── 波形历史 ──────────────────────────────────
HIST_LEN      = 300                     # ~10 s @ 30 fps
hist_ref_yaw  = deque(maxlen=HIST_LEN)
hist_fbd_yaw  = deque(maxlen=HIST_LEN)
hist_ref_pit  = deque(maxlen=HIST_LEN)
hist_fbd_pit  = deque(maxlen=HIST_LEN)

_prev_ref_yaw  = None
_prev_ref_pit  = None
_step_t_yaw    = None;  _step_tgt_yaw = 0.0;  _settle_n_yaw = 0;  resp_yaw_ms = None
_step_t_pit    = None;  _step_tgt_pit = 0.0;  _settle_n_pit = 0;  resp_pit_ms = None
STEP_THR       = 3.0    # deg — 判定阶跃的最小幅度
SETTLE_THR     = 1.5    # deg — 判定已稳定的误差带
SETTLE_N       = 10     # 连续帧数满足稳定条件后记录响应时间

if COM_PORT:
    try:
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=0.1)
        print(f"串口 {COM_PORT} 已连接")
        serial_ok = True
    except Exception as e:
        print(f"串口连接失败: {e}")

PKT_LEN = 16  # 固定包长

def rx_thread():
    global stm32_yaw, stm32_pitch, stm32_fb_valid
    buf = bytearray()
    while running:
        if not serial_ok or ser is None:
            time.sleep(0.1)
            continue
        try:
            n = ser.in_waiting
            if n:
                buf.extend(ser.read(n))
            while len(buf) >= PKT_LEN:
                # 查找帧头 55 AA 00
                idx = -1
                for i in range(len(buf) - 2):
                    if buf[i] == 0x55 and buf[i+1] == 0xAA and buf[i+2] == 0x00:
                        idx = i
                        break
                if idx == -1:
                    buf = buf[-2:]
                    break
                if idx > 0:
                    buf = buf[idx:]
                if len(buf) < PKT_LEN:
                    break
                pkt = buf[:PKT_LEN]
                if pkt[15] == 0xFF:
                    yaw_fb, pitch_fb = struct.unpack_from('<ff', pkt, 3)
                    with stm32_lock:
                        stm32_yaw      = yaw_fb
                        stm32_pitch    = pitch_fb
                        stm32_fb_valid = True
                buf = buf[PKT_LEN:]
        except Exception:
            time.sleep(0.01)

_INVALID_PAYLOAD = b'\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00'  # yaw=NaN, pitch=NaN

def tx_thread():
    global pkt_cnt
    while running:
        if serial_ok and ser is not None:
            with gaze_lock:
                gx, gy    = gaze_x, gaze_y
                is_valid  = gaze_valid
            try:
                if is_valid:
                    yaw, pitch = gaze_to_angle(gx, gy)
                    payload = struct.pack('<ff', yaw, pitch) + b'\x00\x00'
                else:
                    payload = _INVALID_PAYLOAD
                header  = bytes([0x55, 0xAA, 0x01]) + payload
                pkt     = header + bytes([0x00, pkt_cnt & 0xFF, 0xFF])
                ser.write(pkt)
                serial_log.append(f"#{pkt_cnt:03d} " + ' '.join(f'{b:02X}' for b in pkt))
                pkt_cnt += 1
            except Exception as e:
                serial_log.append(f"ERR: {e}")
        time.sleep(TX_INTERVAL)

# ── 眼动仪初始化 ──────────────────────────────
_DLL_NAME    = "tobii_stream_engine.dll"
_COMMON_DIRS = [
    r"C:\Program Files\Tobii\Tobii EyeX",
    r"C:\Program Files (x86)\Tobii\Tobii EyeX",
    r"C:\Program Files\Tobii\Tobii Stream Engine",
    r"C:\Program Files (x86)\Tobii\Tobii Stream Engine",
    r"C:\Program Files\Tobii Pro\Eye Tracker Manager",
    r"C:\Program Files\TobiiTech\Tobii EyeX",
    r"C:\Program Files (x86)\TobiiTech\Tobii EyeX",
]
_WALK_SKIP = frozenset({'$Recycle.Bin', 'System Volume Information', 'WinSxS', 'Windows'})

def _walk_find_dll(root):
    try:
        for dirpath, dirnames, filenames in os.walk(root, onerror=lambda _: None):
            dirnames[:] = [d for d in dirnames if d not in _WALK_SKIP]
            if _DLL_NAME in filenames:
                return os.path.join(dirpath, _DLL_NAME)
    except Exception:
        pass
    return None

def find_tobii_dll():
    # 0. PyInstaller 内嵌（_MEIPASS）
    if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
        p = os.path.join(sys._MEIPASS, _DLL_NAME)
        if os.path.isfile(p):
            print(f"[Tobii] 内嵌 {p}")
            return p

    # 1. exe / 脚本同目录
    exe_dir = (os.path.dirname(sys.executable)
               if getattr(sys, 'frozen', False)
               else os.path.dirname(os.path.abspath(__file__)))
    for d in (exe_dir, os.getcwd()):
        p = os.path.join(d, _DLL_NAME)
        if os.path.isfile(p):
            print(f"[Tobii] {p}")
            return p

    # 2. 常见安装路径
    for d in _COMMON_DIRS:
        p = os.path.join(d, _DLL_NAME)
        if os.path.isfile(p):
            print(f"[Tobii] {p}")
            return p

    # 3. Program Files 目录搜索
    pf_roots = {os.environ.get(v, '')
                for v in ('ProgramFiles', 'ProgramFiles(x86)', 'ProgramW6432')}
    pf_roots.discard('')
    print(f"常见路径未找到 {_DLL_NAME}，搜索 Program Files …")
    for root in pf_roots:
        p = _walk_find_dll(root)
        if p:
            print(f"[Tobii] {p}")
            return p

    # 4. 全盘搜索
    print("未在 Program Files 找到，开始全盘搜索（可能较慢）…")
    drives = [f"{c}:\\" for c in string.ascii_uppercase
              if c not in ('A', 'B') and os.path.exists(f"{c}:\\")]
    for drive in drives:
        p = _walk_find_dll(drive)
        if p:
            print(f"[Tobii] {p}")
            return p

    # 5. 手动输入
    print(f"\n全盘未找到 {_DLL_NAME}")
    while True:
        path = input("请手动输入 tobii_stream_engine.dll 完整路径：").strip('"').strip()
        if os.path.isfile(path):
            return path
        print("  路径不存在，请重试")

dll_path = find_tobii_dll()
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
threading.Thread(target=rx_thread,   daemon=True).start()
threading.Thread(target=tx_thread,   daemon=True).start()

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
C_REF   = (100, 220, 100)   # 波形：参考值（绿）
C_FBD   = (50,  170, 255)   # 波形：反馈值（橙）

CHART_X   = 12
CHART_W   = 300
CHART_H   = 80
CHART_Y0  = 205   # 10 行信息栏底部 ~189 + 16px 间距
CHART_GAP = 6

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

def _val2py(val, cy, ch, y_range):
    c = max(-y_range, min(y_range, val))
    return cy + ch // 2 - int(c / y_range * (ch // 2))

def _draw_chart(frame, cx, cy, cw, ch, ref_buf, fbd_buf, label, y_range):
    roi  = frame[cy:cy+ch, cx:cx+cw]
    dark = roi.copy(); dark[:] = (20, 20, 20)
    cv2.addWeighted(dark, 0.6, roi, 0.4, 0, roi)
    frame[cy:cy+ch, cx:cx+cw] = roi

    cv2.rectangle(frame, (cx, cy), (cx+cw-1, cy+ch-1), (70, 70, 70), 1)
    zy = cy + ch // 2
    cv2.line(frame, (cx+1, zy), (cx+cw-2, zy), (55, 55, 55), 1)
    for frac in (0.5, -0.5):
        ty = _val2py(y_range * frac, cy, ch, y_range)
        cv2.line(frame, (cx+1, ty), (cx+cw-2, ty), (40, 40, 40), 1)

    sx = (cw - 1) / max(HIST_LEN - 1, 1)
    for buf, color in ((ref_buf, C_REF), (fbd_buf, C_FBD)):
        pts    = list(buf)
        nb     = len(pts)
        offset = HIST_LEN - nb
        for i in range(1, nb):
            if math.isnan(pts[i-1]) or math.isnan(pts[i]):
                continue
            x1 = cx + int((offset + i - 1) * sx)
            x2 = cx + int((offset + i    ) * sx)
            y1 = _val2py(pts[i-1], cy, ch, y_range)
            y2 = _val2py(pts[i],   cy, ch, y_range)
            cv2.line(frame, (x1, y1), (x2, y2), color, 1, cv2.LINE_AA)

    pt(frame, label,            (cx+3,      cy+10),    C_GRAY, 0.30)
    pt(frame, f"+{y_range:.0f}", (cx+cw-30, cy+10),   C_GRAY, 0.28)
    pt(frame, f"-{y_range:.0f}", (cx+cw-30, cy+ch-4), C_GRAY, 0.28)

def draw_waveform(frame, ref_yaw, fbd_yaw, fbd_valid, ref_pit, fbd_pit):
    global _prev_ref_yaw, _prev_ref_pit
    global _step_t_yaw, _step_tgt_yaw, _settle_n_yaw, resp_yaw_ms
    global _step_t_pit, _step_tgt_pit, _settle_n_pit, resp_pit_ms

    now = time.time()
    NAN = float('nan')

    hist_ref_yaw.append(ref_yaw)
    hist_fbd_yaw.append(fbd_yaw if fbd_valid else NAN)
    hist_ref_pit.append(ref_pit)
    hist_fbd_pit.append(fbd_pit if fbd_valid else NAN)

    def _update(prev, ref_now, fbd_now, fbd_ok,
                step_t, step_tgt, settle_n, resp_ms):
        if prev is not None and abs(ref_now - prev) > STEP_THR:
            step_t, step_tgt, settle_n, resp_ms = now, ref_now, 0, None
        if step_t is not None and resp_ms is None and fbd_ok:
            if abs(fbd_now - step_tgt) < SETTLE_THR:
                settle_n += 1
                if settle_n >= SETTLE_N:
                    resp_ms = (now - step_t) * 1000
            else:
                settle_n = 0
        return step_t, step_tgt, settle_n, resp_ms

    _step_t_yaw, _step_tgt_yaw, _settle_n_yaw, resp_yaw_ms = _update(
        _prev_ref_yaw, ref_yaw, fbd_yaw, fbd_valid,
        _step_t_yaw, _step_tgt_yaw, _settle_n_yaw, resp_yaw_ms)
    _step_t_pit, _step_tgt_pit, _settle_n_pit, resp_pit_ms = _update(
        _prev_ref_pit, ref_pit, fbd_pit, fbd_valid,
        _step_t_pit, _step_tgt_pit, _settle_n_pit, resp_pit_ms)
    _prev_ref_yaw = ref_yaw
    _prev_ref_pit = ref_pit

    SS = 30
    def _sse(rbuf, fbuf):
        if len(rbuf) < SS or len(fbuf) < SS:
            return None
        r = list(rbuf)[-SS:]
        f = [v for v in list(fbuf)[-SS:] if not math.isnan(v)]
        if len(f) < SS // 2:
            return None
        r_mean = sum(r) / SS
        r_std  = (sum((x - r_mean) ** 2 for x in r) / SS) ** 0.5
        if r_std > 1.5:
            return None
        return r_mean - sum(f) / len(f)

    sse_yaw = _sse(hist_ref_yaw, hist_fbd_yaw)
    sse_pit = _sse(hist_ref_pit, hist_fbd_pit)

    cy_yaw = CHART_Y0
    cy_pit = CHART_Y0 + CHART_H + CHART_GAP
    _draw_chart(frame, CHART_X, cy_yaw, CHART_W, CHART_H,
                hist_ref_yaw, hist_fbd_yaw, "YAW",   60.0)
    _draw_chart(frame, CHART_X, cy_pit, CHART_W, CHART_H,
                hist_ref_pit, hist_fbd_pit, "PITCH", 45.0)

    sy = cy_pit + CHART_H + 10
    cv2.line(frame, (CHART_X,    sy-3), (CHART_X+14, sy-3), C_REF, 2)
    pt(frame, "REF", (CHART_X+17, sy), C_GRAY, 0.32)
    cv2.line(frame, (CHART_X+50, sy-3), (CHART_X+64, sy-3), C_FBD, 2)
    pt(frame, "FBD", (CHART_X+67, sy), C_GRAY, 0.32)

    sy += 16
    sse_y_s = f"{sse_yaw:+.2f}deg" if sse_yaw is not None else "---    "
    sse_p_s = f"{sse_pit:+.2f}deg" if sse_pit is not None else "---    "
    pt(frame, f"SS_ERR  Y:{sse_y_s}  P:{sse_p_s}", (CHART_X, sy), C_WHITE, 0.34)

    sy += 16
    rt_y_s = f"{resp_yaw_ms:.0f}ms" if resp_yaw_ms is not None else "---   "
    rt_p_s = f"{resp_pit_ms:.0f}ms" if resp_pit_ms is not None else "---   "
    pt(frame, f"T_RESP  Y:{rt_y_s}  P:{rt_p_s}", (CHART_X, sy), C_WHITE, 0.34)

def draw_frame(frame):
    global fps
    h, w = frame.shape[:2]
    cx, cy = w // 2, h // 2

    cv2.rectangle(frame, (0, 0), (w-1, h-1), C_CROSS, 1, cv2.LINE_AA)
    draw_crosshair(frame, cx, cy)

    with gaze_lock:
        gx, gy = gaze_x, gaze_y
        valid  = gaze_valid

    with stm32_lock:
        fb_yaw   = stm32_yaw
        fb_pitch = stm32_pitch
        fb_valid = stm32_fb_valid

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

    fb_yaw_col   = C_GREEN if fb_valid and abs(fb_yaw)   < 50 else C_RED
    fb_pitch_col = C_GREEN if fb_valid and abs(fb_pitch) < 35 else C_RED

    rows = [
        ("FPS",    f"{fps}",                         fps_col     ),
        ("GAZE",   f"({gaze_px:>5}, {gaze_py:>5})",  C_GREEN     ),
        ("DX",     f"{dx:>+6} px",                   C_GREEN     ),
        ("DY",     f"{dy:>+6} px",                   C_GREEN     ),
        ("YAW",    f"{yaw:>+8.2f} deg",              yaw_col     ),
        ("PITCH",  f"{pitch:>+8.2f} deg",            pitch_col   ),
        ("FB_YAW", f"{fb_yaw:>+8.2f} deg",           fb_yaw_col  ),
        ("FB_PIT", f"{fb_pitch:>+8.2f} deg",         fb_pitch_col),
        ("PORT",   ser_val,                           ser_col     ),
        ("EYE",    eye_val,                           eye_col     ),
    ]

    for i, (label, value, col) in enumerate(rows):
        draw_row(frame, 18 + i * LH, label, value, col)

    draw_waveform(frame, yaw, fb_yaw, fb_valid, pitch, fb_pitch)

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