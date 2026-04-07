| 步骤                                                         | 备注                                                        |
| ------------------------------------------------------------ | ----------------------------------------------------------- |
| 先usb连接，获取usb IP：10.41.95.1（每次不一样）              | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_1.png) |
| 打开powershell，执行：ssh root@10.41.95.1。密码：root        | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_2.png) |
| 连接wifi：python3 -c "from maix import network; w = network.wifi.Wifi(); w.connect('303-5G', 'langshiyu303'); print(w.get_ip())" | wifi会自动保存配置                                          |
| 直接使用wifi操作摄像头：ssh root@192.168.1.8                 | 无需电脑usb连接了                                           |

**应用上电自启动**

| 步骤                                                         | 备注                                                        |
| ------------------------------------------------------------ | ----------------------------------------------------------- |
| 在maixVerison写代码，打包成应用文件“userVersionApp”并安装。应用ID和应用名称都写“userVersionApp” | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_3.png) |
| 查看已安装应用：ls /maixapp/apps/                            | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_4.png) |
| 设置上电自启动：echo "userversionapp" > /maixapp/auto_start.txt | 查看自启动配置cat /maixapp/auto_start.txt                   |

**模型部署**

| 步骤                                                         | 备注                                                        |
| ------------------------------------------------------------ | ----------------------------------------------------------- |
| 训练好模型后，解压模型文件，把解压的模型文件重命名为userModel | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_5.png) |
| 在.\std_software\version_cam_1\modelScan.py基础上修改。填写标签映射表 | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_6.png) |
| 在maixCam的/root/models/路径上传模型文件                     | ![](C:\11_pro_develop\ezProject_2026\std_img\MaixCAM_7.png) |
| 运行modelScan.py即可执行模型                                 |                                                             |

