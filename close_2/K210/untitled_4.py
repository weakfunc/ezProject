import sensor, image, time, lcd, gc, cmath
from maix import KPU

lcd.init()                          # Init lcd display
lcd.clear(lcd.RED)                  # Clear lcd screen.

# sensor.reset(dual_buff=True)      # improve fps
sensor.reset()                      # Reset and initialize the sensor.
sensor.set_pixformat(sensor.RGB565) # Set pixel format to RGB565 (or GRAYSCALE)
sensor.set_framesize(sensor.QVGA)   # Set frame size to QVGA (320x240)
sensor.set_vflip(False)              # 翻转摄像头
sensor.set_hmirror(False)            # 镜像摄像头
sensor.skip_frames(time = 1000)     # Wait for settings take effect.
clock = time.clock()                # Create a clock object to track the FPS.

print("ready load model")

labels = ["qaingqiu","pingpang","yumao"] #类名称，按照label.txt顺序填写
#anchor = (0.62, 0.77, 1.58, 0.53, 0.89, 1.15, 1.15, 1.47, 3.69, 1.23) # anchors,使用anchor.txt中第二行的值
anchor = (0.65, 0.72, 1.56, 0.59, 1.03, 1.35, 2.40, 0.80, 3.90, 1.27) # anchors,使用anchor.txt中第二行的值

kpu = KPU()
# 从sd或flash加载模型
kpu.load_kmodel('/sd/det.kmodel')
#kpu.load_kmodel(0x300000, 584744)
kpu.init_yolo2(anchor, anchor_num=(int)(len(anchor)/2), img_w=320, img_h=240, net_w=320 , net_h=240 ,layer_w=10 ,layer_h=8, threshold=0.15, nms_value=0.3, classes=len(labels))

from fpioa_manager import fm
# need your connect hardware IO 10/11 to loopback
fm.register(7, fm.fpioa.UART1_TX, force=True)
fm.register(6, fm.fpioa.UART1_RX, force=True)
from machine import UART
uart_A = UART(UART.UART1, 115200, 8, 1, 0, timeout=1000, read_buf_len=4096)
import time
import struct
packTime = 0
isFind = 1

while(True):
    gc.collect()

    clock.tick()
    img = sensor.snapshot()

    kpu.run_with_output(img)
    dect = kpu.regionlayer_yolo2()

    fps = clock.fps()

    img.draw_circle(160, 120, 5, color = (255, 0, 0), thickness = 2, fill = True)

    if len(dect) > 0:
        #for l in dect :
            # 找到概率最高的目标
            best_target = max(dect, key=lambda x: x[5])  # x[5] 是目标置信度

            x, y, w, h, class_id, prob = best_target

            box_center_x = x + w // 2
            box_center_y = y + h // 2

            img.draw_rectangle(x, y, w, h, color=(0, 255, 0))
            img.draw_circle(box_center_x, box_center_y, 5, color = (255, 0, 0), thickness = 2, fill = True)
            img.draw_line(box_center_x, box_center_y, 160, 120, color = (0, 255, 0), thickness = 1)

            dx = box_center_x - 160
            dy = box_center_y - 120
            info = "dx:%d dy:%d %.3f" % (dx, dy, prob)
            img.draw_string(x, y, info, color=(255,0,0), scale=1.0)
            print(info)

            data_packet = bytearray([0xFF]) + struct.pack("hhHb", dx, dy, packTime, class_id) + bytearray([0xFE])
            uart_A.write(data_packet)
            print("数据已发送：", data_packet)
            packTime += 1

            del info

    a = img.draw_string(0, 0, "%2.1ffps" %(fps),color=(0,60,255),scale=2.0)
    lcd.display(img)

uart_A.deinit()
del uart_A
