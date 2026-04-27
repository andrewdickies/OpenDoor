# BOOT 长按网页配网示例

本示例实现以下行为：

- 设备上电后，若已保存 WiFi，则直接以 STA 模式联网。
- 若未保存 WiFi，则自动进入 SoftAP 配网模式。
- 正常运行中，长按 `BOOT`（默认 GPIO0）达到设定时长后，进入配网模式。
- 手机/电脑连接设备 AP 后，浏览器访问 `http://192.168.4.1/`，提交 SSID 和密码。
- 设备收到配置后尝试连接路由器，成功后自动退出配网并回到 STA 模式。

## 配置

在 `idf.py menuconfig` 中进入：

`Boot Button Web Provisioning Example`

可配置项：

- `BOOT button GPIO`（默认 `0`）
- `Long press trigger time (ms)`（默认 `3000`）
- `Provisioning SoftAP SSID`（默认 `OpenDoor-Provision`）
- `Provisioning SoftAP Password`（默认 `12345678`）

## 构建与烧录

```bash
idf.py set-target esp8266
idf.py build
idf.py -p COMx flash monitor
```

## 使用步骤

1. 若设备进入配网模式，串口会输出 AP 信息。
2. 手机/电脑连接设备 AP（默认 `OpenDoor-Provision`）。
3. 浏览器打开 `http://192.168.4.1/`。
4. 输入目标路由器 SSID 和密码并提交。
5. 观察串口日志，出现 `STA got ip` 即表示联网成功。

## 注意事项

- `BOOT`（GPIO0）是启动相关管脚，务必在“设备已正常运行后”长按触发，不要在上电复位瞬间一直拉低。
- 示例将 WiFi 凭据保存到 NVS，后续重启会直接尝试连接已保存网络。
