import cv2
import numpy as np

cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

cap.set(cv2.CAP_PROP_BRIGHTNESS,  0)
cap.set(cv2.CAP_PROP_CONTRAST,    90)
cap.set(cv2.CAP_PROP_SATURATION,  80)
cap.set(cv2.CAP_PROP_SHARPNESS,   0)
cap.set(cv2.CAP_PROP_GAMMA,       40)

cv2.namedWindow("Camera", cv2.WINDOW_NORMAL)
cv2.resizeWindow("Camera", 1280, 720)

if not cap.isOpened():
    print("无法打开摄像头")
    exit()

debug_mode = False

def draw_crosshair(frame, color=(0, 255, 0), size=30, gap=8, thickness=1):
    h, w = frame.shape[:2]
    cx, cy = w // 2, h // 2
    cv2.line(frame, (cx - size, cy), (cx - gap, cy), color, thickness)
    cv2.line(frame, (cx + gap, cy), (cx + size, cy), color, thickness)
    cv2.line(frame, (cx, cy - size), (cx, cy - gap), color, thickness)
    cv2.line(frame, (cx, cy + gap), (cx, cy + size), color, thickness)

def detect_red(frame):
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    # 更严格的红色范围：提高饱和度和亮度下限，排除橙色
    mask1 = cv2.inRange(hsv, np.array([0,   150, 100]), np.array([5,  255, 255]))
    mask2 = cv2.inRange(hsv, np.array([170, 150, 100]), np.array([180,255, 255]))
    mask = cv2.bitwise_or(mask1, mask2)
    kernel = np.ones((7, 7), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN,  kernel)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
    return mask

def draw_debug(frame):
    h, w = frame.shape[:2]
    cx, cy = w // 2, h // 2

    # 网格
    for i in range(1, 3):
        cv2.line(frame, (w * i // 3, 0), (w * i // 3, h), (50, 50, 50), 1)
        cv2.line(frame, (0, h * i // 3), (w, h * i // 3), (50, 50, 50), 1)

    # 中心点
    cv2.circle(frame, (cx, cy), 4, (0, 0, 255), -1)

    # 红色检测
    mask = detect_red(frame)
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    for cnt in contours:
        if cv2.contourArea(cnt) < 3000:  # 提高面积阈值
            continue

        x, y, bw, bh = cv2.boundingRect(cnt)
        box_cx = x + bw // 2
        box_cy = y + bh // 2

        dx = box_cx - cx
        dy = box_cy - cy

        cv2.rectangle(frame, (x, y), (x + bw, y + bh), (0, 80, 255), 2)
        cv2.circle(frame, (box_cx, box_cy), 4, (0, 80, 255), -1)
        cv2.line(frame, (cx, cy), (box_cx, box_cy), (255, 255, 0), 1)

        label = f"dx={dx:+d}  dy={dy:+d}"
        cv2.putText(frame, label, (x, y - 10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 80, 255), 1)

    cv2.putText(frame, f"Resolution: {w}x{h}", (10, 25),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 0), 1)
    cv2.putText(frame, "[DEBUG MODE]  Press D to switch", (10, h - 15),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 100, 255), 1)

print("按 D 切换视图，按 Q 退出")

while True:
    ret, frame = cap.read()
    if not ret:
        break

    if debug_mode:
        draw_debug(frame)
    else:
        draw_crosshair(frame)
        cv2.putText(frame, "Press D for debug", (10, 25),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.4, (100, 100, 100), 1)

    cv2.imshow("Camera", frame)

    key = cv2.waitKey(1) & 0xFF
    if key == ord('q'):
        break
    elif key == ord('d'):
        debug_mode = not debug_mode
        print(f"切换到{'Debug' if debug_mode else '用户'}视图")

cap.release()
cv2.destroyAllWindows()