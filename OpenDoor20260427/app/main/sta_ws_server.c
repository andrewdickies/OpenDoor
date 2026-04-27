#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"

#include "esp_log.h"

#include "config.h"
#include "sta_ws_server.h"
#include "time_sync.h"

static const char *TAG = "sta_ws_srv";
static volatile bool s_serverRun;
static TaskHandle_t s_serverTask;
static int s_listenSock = -1;
static sta_ws_open_door_cb_t s_openDoorCb;
static sta_ws_close_door_cb_t s_closeDoorCb;
static sta_ws_status_json_cb_t s_statusJsonCb;
static sta_ws_ota_set_url_cb_t s_otaSetUrlCb;
static sta_ws_ota_start_cb_t s_otaStartCb;
static sta_ws_ota_status_json_cb_t s_otaStatusCb;

static const char s_httpPage[] =
	"<!doctype html><html><head><meta charset='utf-8'>"
	"<meta name='viewport' content='width=device-width,initial-scale=1'>"
	"<title>OpenDoor STA</title>"
	"<style>"
	"*{box-sizing:border-box;}"
	"body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:#f3f4f6;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;padding:12px;}"
	".card{width:min(520px,100%);background:#fff;border-radius:16px;padding:18px;box-shadow:0 10px 30px rgba(0,0,0,.08);}"
	"h2{margin:0 0 14px 0;font-size:22px;}"
	".row{display:flex;align-items:center;gap:10px;margin:10px 0;padding:10px 12px;background:#f9fafb;border-radius:12px;}"
	".switch{position:relative;display:inline-block;width:52px;height:30px;}"
	".switch input{opacity:0;width:0;height:0;}"
	".slider{position:absolute;cursor:pointer;inset:0;background:#ccc;transition:.2s;border-radius:30px;}"
	".slider:before{position:absolute;content:'';height:24px;width:24px;left:3px;top:3px;background:white;transition:.2s;border-radius:50%;}"
	"input:checked + .slider{background:#34c759;}"
	"input:checked + .slider:before{transform:translateX(22px);}"
	".state{font-weight:600;min-width:72px;}"
	".otaRow{display:flex;gap:8px;margin:10px 0;}"
	"#ota{flex:1;padding:10px 12px;border:1px solid #d1d5db;border-radius:10px;font-size:14px;}"
	"button{border:none;background:#2563eb;color:#fff;border-radius:10px;padding:10px 14px;font-size:14px;cursor:pointer;}"
	"button:hover{background:#1d4ed8;}"
	"#otaText{margin-top:8px;padding:10px 12px;background:#eef2ff;border-radius:10px;color:#1e3a8a;}"
	"#status{margin-top:10px;padding:10px 12px;background:#111827;color:#d1d5db;border-radius:10px;max-height:220px;overflow:auto;font-size:12px;}"
	"</style></head><body>"
	"<div class='card'>"
	"<h2>OpenDoor Control</h2>"
	"<div class='row'>"
	"<span class='state' id='doorState'>门已关闭</span>"
	"<label class='switch'>"
	"<input id='doorToggle' type='checkbox' onchange='toggleDoor(this.checked)'>"
	"<span class='slider'></span>"
	"</label>"
	"</div>"
	"<div class='otaRow'><input id='ota' value='" APP_OTA_DEFAULT_URL "' placeholder='http://host/app.bin'><button onclick=\"upgradeOta()\">升级</button></div>"
	"<div>"
	"<div id='otaText'>OTA: idle</div>"
	"<div id='deviceTime' style='margin-top:6px;color:#374151'>设备时间: --</div>"
	"</div>"
	"<pre id='status'>connecting...</pre>"
	"<script>"
	"let ws;"
	"let ignoreToggleEvent=false;"
	"let pollTimer=null;"
	"function setDoorView(isOpen){"
	"ignoreToggleEvent=true;"
	"const t=document.getElementById('doorToggle');"
	"t.checked=!!isOpen;"
	"document.getElementById('doorState').textContent=isOpen?'门已打开':'门已关闭';"
	"setTimeout(()=>{ignoreToggleEvent=false;},0);"
	"}"
	"function render(data){"
	"document.getElementById('status').textContent=JSON.stringify(data,null,2);"
	"if(data&&data.door&&typeof data.door.doorOpen==='boolean'){setDoorView(data.door.doorOpen);}"
	"if(data&&data.time){"
	"let ts=(typeof data.time.unix==='number')?data.time.unix:0;"
	"let iso=(typeof data.time.iso==='string')?data.time.iso:'--';"
	"document.getElementById('deviceTime').textContent='设备时间: '+iso+' (unix='+ts+')';"
	"}"
	"if(data&&data.ota){"
	"let w=(typeof data.ota.written==='number')?data.ota.written:0;"
	"let t=(typeof data.ota.total==='number')?data.ota.total:-1;"
	"let s=(typeof data.ota.speedBps==='number')?data.ota.speedBps:0;"
	"let text='OTA: state='+data.ota.state+' written='+w+'B';"
	"if(t>=0){text+=' total='+t+'B';}"
	"text+=' speed='+s+'B/s';"
	"document.getElementById('otaText').textContent=text;"
	"}"
	"}"
	"function connect(){"
	"ws=new WebSocket('ws://'+location.host+'/ws');"
	"ws.onopen=()=>ws.send('status');"
	"ws.onmessage=(e)=>{try{render(JSON.parse(e.data));}catch(_){document.getElementById('status').textContent=e.data;}};"
	"ws.onclose=()=>{if(pollTimer){clearInterval(pollTimer);pollTimer=null;}setTimeout(connect,1000);};"
	"if(!pollTimer){pollTimer=setInterval(()=>{if(ws&&ws.readyState===1){ws.send('status');}},1000);}"
	"}"
	"function toggleDoor(checked){"
	"if(ignoreToggleEvent){return;}"
	"if(ws&&ws.readyState===1){ws.send(checked?'open':'close');}"
	"}"
	"function upgradeOta(){if(ws&&ws.readyState===1){ws.send('ota_upgrade:'+document.getElementById('ota').value);}}"
	"connect();"
	"</script></div></body></html>";

static int socket_recv_line(int sock, char *buf, size_t size)
{
	size_t i = 0;
	while (i + 1 < size) {
		char c;
		int ret = recv(sock, &c, 1, 0);
		if (ret <= 0) {
			return -1;
		}
		buf[i++] = c;
		if (c == '\n') {
			break;
		}
	}
	buf[i] = '\0';
	return (int) i;
}

static bool http_get_header_value(const char *req, const char *key, char *out, size_t outSize)
{
	const char *pos = strstr(req, key);
	if (!pos) {
		return false;
	}
	pos += strlen(key);
	while (*pos == ' ') {
		pos++;
	}
	const char *end = strstr(pos, "\r\n");
	if (!end) {
		return false;
	}
	size_t len = (size_t) (end - pos);
	if (len >= outSize) {
		len = outSize - 1;
	}
	memcpy(out, pos, len);
	out[len] = '\0';
	return true;
}

static void build_ws_accept(const char *clientKey, char *out, size_t outSize)
{
	static const char *magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	unsigned char sha1[20];
	unsigned char keyBuf[128];
	size_t olen = 0;

	snprintf((char *) keyBuf, sizeof(keyBuf), "%s%s", clientKey, magic);
	mbedtls_sha1(keyBuf, strlen((const char *) keyBuf), sha1);
	mbedtls_base64_encode((unsigned char *) out, outSize, &olen, sha1, sizeof(sha1));
	out[olen] = '\0';
}

static void send_http_response(int sock, const char *contentType, const char *body)
{
	char header[192];
	snprintf(header, sizeof(header),
		"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
		contentType, (int) strlen(body));
	send(sock, header, strlen(header), 0);
	send(sock, body, strlen(body), 0);
}

static bool websocket_recv_text(int sock, char *out, size_t outSize)
{
	unsigned char hdr[2];
	unsigned char mask[4];
	unsigned char payload[320];
	size_t payloadLen;
	int i;

	if (recv(sock, hdr, 2, MSG_WAITALL) != 2) {
		return false;
	}
	payloadLen = hdr[1] & 0x7F;
	if (!(hdr[1] & 0x80) || payloadLen == 126 || payloadLen == 127 || payloadLen >= sizeof(payload)) {
		return false;
	}
	if (recv(sock, mask, 4, MSG_WAITALL) != 4) {
		return false;
	}
	if (recv(sock, payload, payloadLen, MSG_WAITALL) != (int) payloadLen) {
		return false;
	}

	for (i = 0; i < (int) payloadLen; i++) {
		payload[i] ^= mask[i % 4];
	}
	if (payloadLen >= outSize) {
		payloadLen = outSize - 1;
	}
	memcpy(out, payload, payloadLen);
	out[payloadLen] = '\0';
	return true;
}

static void websocket_send_text(int sock, const char *text)
{
	unsigned char hdr[4];
	size_t len = strlen(text);
	size_t hdrLen = 2;

	if (len <= 125) {
		hdr[0] = 0x81;
		hdr[1] = (unsigned char) len;
	} else if (len <= 65535) {
		hdr[0] = 0x81;
		hdr[1] = 126;
		hdr[2] = (unsigned char) ((len >> 8) & 0xFF);
		hdr[3] = (unsigned char) (len & 0xFF);
		hdrLen = 4;
	} else {
		len = 65535;
		hdr[0] = 0x81;
		hdr[1] = 126;
		hdr[2] = 0xFF;
		hdr[3] = 0xFF;
		hdrLen = 4;
	}

	send(sock, hdr, hdrLen, 0);
	send(sock, text, len, 0);
}

static bool build_status_payload(char *out, size_t outSize)
{
	char statusBuf[160];
	char otaStatusBuf[220];
	char timeBuf[96];
	time_t now = time_sync_now();

	if (!s_statusJsonCb || !s_otaStatusCb) {
		return false;
	}
	s_statusJsonCb(statusBuf, sizeof(statusBuf));
	s_otaStatusCb(otaStatusBuf, sizeof(otaStatusBuf));
	time_sync_format_local(timeBuf, sizeof(timeBuf));
	snprintf(out, outSize, "{\"door\":%s,\"ota\":%s,\"time\":{\"unix\":%lu,\"iso\":\"%s\"}}",
		statusBuf, otaStatusBuf, (unsigned long) now, timeBuf);
	return true;
}

static void websocket_session(int sock)
{
	char wsCmdBuf[320];
	char mergedBuf[420];

	if (build_status_payload(mergedBuf, sizeof(mergedBuf))) {
		websocket_send_text(sock, mergedBuf);
	}

	while (s_serverRun && websocket_recv_text(sock, wsCmdBuf, sizeof(wsCmdBuf))) {
		if (strcmp(wsCmdBuf, "open") == 0 && s_openDoorCb) {
			s_openDoorCb("websocket");
		}
		if (strcmp(wsCmdBuf, "close") == 0 && s_closeDoorCb) {
			s_closeDoorCb("websocket");
		}
		if (strncmp(wsCmdBuf, "ota_upgrade:", 12) == 0 && s_otaSetUrlCb && s_otaStartCb) {
			if (s_otaSetUrlCb(wsCmdBuf + 12) == ESP_OK) {
				s_otaStartCb();
			}
		}
		if (build_status_payload(mergedBuf, sizeof(mergedBuf))) {
			websocket_send_text(sock, mergedBuf);
		}
	}
}

static void handle_http_client(int sock)
{
	char reqBuf[1024];
	char lineBuf[256];
	char wsKeyBuf[128];
	char mergedBuf[420];
	char acceptBuf[128];
	char respBuf[256];
	int n;

	memset(reqBuf, 0, sizeof(reqBuf));
	while (1) {
		n = socket_recv_line(sock, lineBuf, sizeof(lineBuf));
		if (n <= 0) {
			return;
		}
		if (strlen(reqBuf) + strlen(lineBuf) + 1 < sizeof(reqBuf)) {
			strcat(reqBuf, lineBuf);
		}
		if (strcmp(lineBuf, "\r\n") == 0) {
			break;
		}
	}

	if (strstr(reqBuf, "GET /ws ") && http_get_header_value(reqBuf, "Sec-WebSocket-Key:", wsKeyBuf, sizeof(wsKeyBuf))) {
		build_ws_accept(wsKeyBuf, acceptBuf, sizeof(acceptBuf));
		snprintf(respBuf, sizeof(respBuf),
			"HTTP/1.1 101 Switching Protocols\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Accept: %s\r\n\r\n",
			acceptBuf);
		send(sock, respBuf, strlen(respBuf), 0);
		websocket_session(sock);
		return;
	}

	if (strstr(reqBuf, "GET /status ")) {
		if (!build_status_payload(mergedBuf, sizeof(mergedBuf))) {
			strlcpy(mergedBuf, "{\"door\":{\"openCount\":0},\"ota\":{\"state\":0}}", sizeof(mergedBuf));
		}
		send_http_response(sock, "application/json", mergedBuf);
		return;
	}

	if (strstr(reqBuf, "POST /open ")) {
		if (s_openDoorCb) {
			s_openDoorCb("web_http");
		}
		send_http_response(sock, "application/json", "{\"ok\":true}");
		return;
	}
	if (strstr(reqBuf, "POST /close ")) {
		if (s_closeDoorCb) {
			s_closeDoorCb("web_http");
		}
		send_http_response(sock, "application/json", "{\"ok\":true}");
		return;
	}
	if (strstr(reqBuf, "POST /ota/start ")) {
		if (s_otaStartCb) {
			s_otaStartCb();
		}
		send_http_response(sock, "application/json", "{\"ok\":true}");
		return;
	}
	if (strstr(reqBuf, "POST /ota/url ")) {
		const char *body = strstr(reqBuf, "\r\n\r\n");
		if (body && s_otaSetUrlCb) {
			body += 4;
			s_otaSetUrlCb(body);
		}
		send_http_response(sock, "application/json", "{\"ok\":true}");
		return;
	}

	send_http_response(sock, "text/html; charset=utf-8", s_httpPage);
}

static void sta_ws_client_task(void *arg)
{
	int clientSock = (int) (intptr_t) arg;
	handle_http_client(clientSock);
	close(clientSock);
	vTaskDelete(NULL);
}

static void sta_ws_server_task(void *arg)
{
	struct sockaddr_in addr;
	int reuse = 1;

	s_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (s_listenSock < 0) {
		ESP_LOGE(TAG, "socket create failed");
		vTaskDelete(NULL);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(80);

	setsockopt(s_listenSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	if (bind(s_listenSock, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
		ESP_LOGE(TAG, "bind failed");
		close(s_listenSock);
		s_listenSock = -1;
		s_serverTask = NULL;
		vTaskDelete(NULL);
		return;
	}
	if (listen(s_listenSock, 2) != 0) {
		ESP_LOGE(TAG, "listen failed");
		close(s_listenSock);
		s_listenSock = -1;
		s_serverTask = NULL;
		vTaskDelete(NULL);
		return;
	}
	ESP_LOGI(TAG, "STA web+ws server started on :80");

	while (s_serverRun) {
		int clientSock = accept(s_listenSock, NULL, NULL);
		if (clientSock < 0) {
			if (!s_serverRun) {
				break;
			}
			continue;
		}
		if (xTaskCreate(sta_ws_client_task, "sta_ws_cli", 5120, (void *) (intptr_t) clientSock, 4, NULL) != pdPASS) {
			ESP_LOGW(TAG, "client task create failed, closing socket");
			close(clientSock);
		}
	}

	if (s_listenSock >= 0) {
		close(s_listenSock);
		s_listenSock = -1;
	}
	s_serverTask = NULL;
	vTaskDelete(NULL);
}

esp_err_t sta_ws_server_start(
	sta_ws_open_door_cb_t openDoorCb,
	sta_ws_close_door_cb_t closeDoorCb,
	sta_ws_status_json_cb_t statusJsonCb,
	sta_ws_ota_set_url_cb_t otaSetUrlCb,
	sta_ws_ota_start_cb_t otaStartCb,
	sta_ws_ota_status_json_cb_t otaStatusCb)
{
	if (s_serverTask) {
		return ESP_OK;
	}
	s_openDoorCb = openDoorCb;
	s_closeDoorCb = closeDoorCb;
	s_statusJsonCb = statusJsonCb;
	s_otaSetUrlCb = otaSetUrlCb;
	s_otaStartCb = otaStartCb;
	s_otaStatusCb = otaStatusCb;
	s_serverRun = true;
	xTaskCreate(sta_ws_server_task, "sta_ws_srv", 6144, NULL, 4, &s_serverTask);
	return ESP_OK;
}

void sta_ws_server_stop(void)
{
	s_serverRun = false;
	if (s_listenSock >= 0) {
		shutdown(s_listenSock, SHUT_RDWR);
	}
	for (int i = 0; i < 20 && s_serverTask != NULL; i++) {
		vTaskDelay(pdMS_TO_TICKS(50));
	}
	if (s_serverTask != NULL) {
		ESP_LOGW(TAG, "force stop sta ws server task");
		vTaskDelete(s_serverTask);
		s_serverTask = NULL;
	}
	if (s_listenSock >= 0) {
		close(s_listenSock);
		s_listenSock = -1;
	}
}
