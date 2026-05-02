import ctypes
import time
import math

dll_path = r"C:\Program Files\Tobii\Tobii EyeX\tobii_stream_engine.dll"
tobii = ctypes.CDLL(dll_path)

class TobiiGazePoint(ctypes.Structure):
    _fields_ = [
        ("timestamp_us", ctypes.c_int64),
        ("validity", ctypes.c_int),
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
    ]

GAZE_CALLBACK = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(TobiiGazePoint),
    ctypes.c_void_p
)

def gaze_callback(gaze_point, user_data):
    g = gaze_point.contents
    if not (math.isnan(g.x) or math.isnan(g.y)):
        print(f"validity={g.validity} gaze: ({g.x:.4f}, {g.y:.4f})")

cb = GAZE_CALLBACK(gaze_callback)

api = ctypes.c_void_p()
tobii.tobii_api_create(ctypes.byref(api), None, None)

urls = (ctypes.c_char_p * 16)()
url_count = ctypes.c_int(0)

def url_receiver(url, user_data):
    urls[url_count.value] = url
    url_count.value += 1

URL_RECEIVER = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_void_p)
url_cb = URL_RECEIVER(url_receiver)
tobii.tobii_enumerate_local_device_urls(api, url_cb, None)

device = ctypes.c_void_p()
tobii.tobii_device_create(api, urls[0], ctypes.c_int(1), ctypes.byref(device))

ret = tobii.tobii_gaze_point_subscribe(device, cb, None)
print(f"subscribe ret={ret}")

print("开始读取，眼睛盯着屏幕...")
for _ in range(100):
    tobii.tobii_wait_for_callbacks(ctypes.c_int(1), ctypes.byref(device))
    tobii.tobii_device_process_callbacks(device)
    time.sleep(0.03)

tobii.tobii_device_destroy(device)
tobii.tobii_api_destroy(api)