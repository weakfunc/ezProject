| 步骤                                                         | 备注                                                        |
| ------------------------------------------------------------ | ----------------------------------------------------------- |
| 先usb连接，获取usb IP：10.41.95.1                            | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_1.png) |
| 打开powershell，执行：ssh root@10.41.95.1                    | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_2.png) |
| 连接wifi：python3 -c "from maix import network; w = network.wifi.Wifi(); w.connect('303-5G', 'langshiyu303'); print(w.get_ip())" | wifi会自动保存配置                                          |
|                                                              |                                                             |
| 直接使用wifi操作摄像头：ssh root@192.168.1.8                 | 无需电脑usb连接了                                           |
| 在maixVerison写代码，打包成应用文件“userVersionApp”并安装。应用ID和应用名称都写“userVersionApp” | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_3.png) |
| 查看已安装应用：ls /maixapp/apps/                            | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_4.png) |
| 设置上电自启动：echo "userversionapp" > /maixapp/auto_start.txt |                                                             |

