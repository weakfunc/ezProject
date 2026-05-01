1. For detailed information about the SDK's components, features, and development, please refer to the SDK documentation for the corresponding chip series in the Software Development section of the Docume
   Taking CI13060 as an example, the reference link is as follows: https://document.chipintelli.com/%E8%BD%AF%E4%BB%B6%E5%BC%80%E5%8F%91/SDK/CI130X%E8%8A%AF%E7%89%87SDK/CI-SDK-Offline/

2. If you need to modify and adjust the relevant algorithm parameters in the SDK, please refer to the SDK documentation for the corresponding chip series in the Software Development section of the Documentation Center.
   Taking AEC as an example, the reference link is as follows:：https://document.chipintelli.com/%E8%BD%AF%E4%BB%B6%E5%BC%80%E5%8F%91/SDK/CI130X%E8%8A%AF%E7%89%87SDK/components/%E5%9B%9E%E5%A3%B0%E6%B6%88%E9%99%A4%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E/

3. If the firmware size exceeds the flash capacity of the selected chip, please try again after switching to a smaller acoustic model, reducing the number of command words or voice prompts, or using a chip with larger flash capacity.

4. If algorithms are enabled, the system provides relatively less memory space for language model. To ensure normal recognition operation, please reduce the number of command words.


The firmware communication protocol is as follows:
<welcome>:AA 55 01 00 FB:AA 55 01 00 FB
<inactivate>:AA 55 01 01 FB:AA 55 01 01 FB
小二同学:AA 55 01 02 FB:AA 55 01 02 FB
增大音量:AA 55 01 03 FB:AA 55 01 03 FB
减小音量:AA 55 01 04 FB:AA 55 01 04 FB
最大音量:AA 55 01 05 FB:AA 55 01 05 FB
中等音量:AA 55 01 06 FB:AA 55 01 06 FB
最小音量:AA 55 01 07 FB:AA 55 01 07 FB
开启播报:AA 55 01 08 FB:AA 55 01 08 FB
关闭播报:AA 55 01 09 FB:AA 55 01 09 FB
自定义一:FF 00 00 00 FF:FF 00 00 00 FF
自定义二:FF 00 01 00 FF:FF 00 01 00 FF
自定义三:FF 00 02 00 FF:FF 00 02 00 FF
自定义四:FF 00 03 00 FF:FF 00 03 00 FF
自定义五:FF 00 04 00 FF:FF 00 04 00 FF
自定义六:FF 00 05 00 FF:FF 00 05 00 FF
自定义七:FF 00 06 00 FF:FF 00 06 00 FF
自定义八:FF 00 07 00 FF:FF 00 07 00 FF
自定义九:FF 00 08 00 FF:FF 00 08 00 FF
自定义十:FF 00 09 00 FF:FF 00 09 00 FF
自定义十一:FF 00 0A 00 FF:FF 00 0A 00 FF
自定义十二:FF 00 0B 00 FF:FF 00 0B 00 FF
自定义十三:FF 00 0C 00 FF:FF 00 0C 00 FF
自定义十四:FF 00 0D 00 FF:FF 00 0D 00 FF
自定义十五:FF 00 0E 00 FF:FF 00 0E 00 FF
自定义十六:FF 00 0F 00 FF:FF 00 0F 00 FF
自定义十七:FF 00 10 00 FF:FF 00 10 00 FF
右侧步:FF 00 11 00 FF:FF 00 11 00 FF
左侧步:FF 00 12 00 FF:FF 00 12 00 FF
前进:FF 00 13 00 FF:FF 00 13 00 FF
后退:FF 00 14 00 FF:FF 00 14 00 FF
翻跟头:FF 00 15 00 FF:FF 00 15 00 FF
仰卧起坐:FF 00 16 00 FF:FF 00 16 00 FF
俯卧撑:FF 00 17 00 FF:FF 00 17 00 FF
后滚翻:FF 00 18 00 FF:FF 00 18 00 FF
左边攻击:FF 00 19 00 FF:FF 00 19 00 FF
右边攻击:FF 00 1A 00 FF:FF 00 1A 00 FF
左连击:FF 00 1B 00 FF:FF 00 1B 00 FF
右连击:FF 00 1C 00 FF:FF 00 1C 00 FF
前攻击:FF 00 1D 00 FF:FF 00 1D 00 FF
左踢:FF 00 1E 00 FF:FF 00 1E 00 FF
右踢:FF 00 1F 00 FF:FF 00 1F 00 FF
左侧踢:FF 00 20 00 FF:FF 00 20 00 FF
右侧踢:FF 00 21 00 FF:FF 00 21 00 FF
格挡:FF 00 22 00 FF:FF 00 22 00 FF
左格挡:FF 00 23 00 FF:FF 00 23 00 FF
右格挡:FF 00 24 00 FF:FF 00 24 00 FF
再次格挡:FF 00 25 00 FF:FF 00 25 00 FF
倒立:FF 00 26 00 FF:FF 00 26 00 FF
快速前进:FF 00 27 00 FF:FF 00 27 00 FF
街舞:FF 00 28 00 FF:FF 00 28 00 FF
来段舞蹈吧:FF 00 29 00 FF:FF 00 29 00 FF
来段舞蹈:FF 00 2A 00 FF:FF 00 2A 00 FF
