#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Servo.h>
#include <EEPROM.h>

#define SERVO D8
const int lightPins [] = {
  D7, D6, D5, D2, D1, D0
};
const String lightsNames [] = {
  "MOON_HALF_COLD",
  "MOON_HALF_WARM",
  "MOON_FULL_COLD",
  "MOON_FULL_WARM",
  "SUN_COLD",
  "SUN_WARM"
};
const int lightMaxLevel = 8;
const int lightPwmStep = 32;
const long tickInterval = 20;
const long blinkInterval = 200;
const int maxMoveTime = 950 * 3;
const char* ssid     = "Sun Moon Lamp";
const char* password = "123456789";
String newHostname = "Sun Moon Lamp";

int lightLevels [] = {
  0, 0, 0, 0, 0, 0
};
boolean blinker = false;
boolean blinkState = true;

int motorHaltAngle = 86;
int motorSpeedAngle = 20;
unsigned long lastMoveCommandTime;
int lastMoveDirection;
int moveDuration;
boolean timedMove;
boolean servoMoving;
Servo servo;

String wifiUser;
String wifiPass;

AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  
  <style>
  
  </style>
</head>
<body>
  <h1>Sun/Moon Lamp</h1>
  <p>
    <h3>WiFi</h3>
    <input type="text" id="user" value="%USER%" /><br/>
    <input type="password" id="pass" value="%PASS%" /> <button onclick="return revealPass()">Reveal</button><br/>
    <button onclick="return connect()">Connect</button>
    <div id="wifi-message"></div>
  </p>
  <p>
    <h2>Preset</h2>
    <button onclick="return setPreset('off')">Off</button>
    <button onclick="return setPreset('sun')">Sun</button>
    <button onclick="return setPreset('sunset')">Sunset</button>
    <button onclick="return setPreset('fullmoon')">Full Moon</button>
    <button onclick="return setPreset('gibous')">Waning Gibous</button>
    <button onclick="return setPreset('quarter')">Third Quater</button>
    <button onclick="return setPreset('crescent')">Waning Crescent</button>
  </p>
  <p>
    <h2>Motor</h2>
    <button onclick="return moveMotor('back')">&lt;</button>
    <button onclick="return moveMotor('stop')">Stop</button>
    <button onclick="return moveMotor('forward')">&gt;</button>
    <h3>Callibrate</h3>
    <input type="number" value="%HALT_ANGLE%" onchange="return calibrateMotor('halt', this.value)" />
    <input type="number" value="%SPEED_ANGLE%" onchange="return calibrateMotor('speed', this.value)" />
  </p>
  <p>
    <h2>Light</h2>
    %MOON_HALF_COLD%
    %MOON_HALF_WARM%
    %MOON_FULL_COLD%
    %MOON_FULL_WARM%
    %SUN_COLD%
    %SUN_WARM%
  </p>
  <p>
    <h2>On-board LED</h2>
    <button onclick="return toggleStatusBlinkLed()">Toggle Blink Led</button>
  </p>
</body>
<script>

function revealPass() {
  var input = document.getElementById("pass");
  if (input.type == "password") input.type = "text";
  else input.type = "password";
  return false;
}

function connect(){
  var user = document.getElementById("user").value;
  var pass = document.getElementById("pass").value;
  
  document.getElementById("wifi-message").innerText = "Connecting to wifi " + user + " ...";

  var xhttp = new XMLHttpRequest();
  xhttp.open("POST", "/wifi", true);
  xhttp.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
  xhttp.send("user=" + encodeURIComponent(user) + "&pass=" + encodeURIComponent(pass));
  return false;
}

function setPreset(preset) {
  var xhttp = new XMLHttpRequest();
  xhttp.open("GET", "/preset?name=" + preset, true);
  xhttp.send();
  return false;
}

function moveMotor(command) {
  var xhttp = new XMLHttpRequest();
  xhttp.open("GET", "/motor?command=" + command, true);
  xhttp.send();
  return false;
}

function calibrateMotor(name, value) {
  var xhttp = new XMLHttpRequest();
  xhttp.open("GET", "/motor/" + name + "?value=" + value, true);
  xhttp.send();
  return false;
}

function toggleChannel(chann, level) {
var xhttp = new XMLHttpRequest();
  xhttp.open("GET", "/light?channel=" + chann + "&level=" + level, true);
  xhttp.send();
  return false;
}

function toggleStatusBlinkLed() {
  var xhttp = new XMLHttpRequest();
  xhttp.open("GET", "/blink", true);
  xhttp.send();
  return false;
}

</script>
</html>)rawliteral";

String processor(const String& var) {
  if (var == "HALT_ANGLE") {
    return String(motorHaltAngle);
  } else if (var == "SPEED_ANGLE") {
    return String(motorSpeedAngle);
  } else if (var == "USER") {
    return wifiUser;
  } else if (var == "PASS") {
    return wifiPass;
  }
  for (int i = 0; i < 6; i++) {
    if (var == lightsNames[i]) {
      String ret = "<h3>Channel " + String(i + 1) + "</h3>";
      for (int j = 0; j < lightMaxLevel + 1; j++) {
        ret = ret + "<button onclick=\"return toggleChannel(" + String(i) + "," + String(j) + ")\">" + String(j) + "</button>";
      }
      return ret;
    }
  }
  return String();
}

void setup() {
  initLights();

  Serial.begin(115200);
  delay(1000);

  rst_info *rinfo;
  rinfo = ESP.getResetInfoPtr();
  int rstReason = (*rinfo).reason;
  Serial.println("");
  Serial.println(String("ResetInfo.reason = ") + rstReason);

  EEPROM.begin(512);

  wifiUser = readForStore(0, 20);
  wifiPass = readForStore(20, 20);

  if (rstReason == 6 && wifiUser.length() > 0) {
    clearMemory(0, 40);
    wifiUser = "";
    wifiPass = "";
  }

  if (wifiUser.length() > 0) {
    Serial.println("Connecting to " + wifiUser + "…");
    WiFi.hostname(newHostname.c_str());
    WiFi.begin(wifiUser, wifiPass);
    int attempts = 300000 / blinkInterval;
    while (WiFi.status() != WL_CONNECTED) {
      if (attempts <= 0) {
        Serial.println("Can not connect to wifi, restarting ...");
        clearMemory(0, 40);
        ESP.restart();
        return;
      }
      toggleStatusBlink();
      delay(blinkInterval);
      attempts--;
    }
    setAllLightsOff();
    Serial.println("WiFi connected");
    Serial.print("Got IP: ");
    Serial.println(WiFi.localIP());
    blinker = false;

  } else {
    Serial.println("Setting AP…");
    WiFi.softAP(ssid, password);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    blinker = true;
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest * request) {
    String user = request->arg("user");
    String pass = request->arg("pass");
    saveWifiCreds(user, pass);
    request->send_P(200, "text/plain", "OK");
  });
  server.on("/preset", HTTP_GET, [](AsyncWebServerRequest * request) {
    AsyncWebParameter* name = request->getParam("name");
    applyPreset(name->value());
    request->send_P(200, "text/plain", "OK");
  });
  server.on("/light", HTTP_GET, [](AsyncWebServerRequest * request) {
    AsyncWebParameter* chan = request->getParam("channel");
    AsyncWebParameter* level = request->getParam("level");
    setLightChannelLevelAndCommitStr(chan->value(), level->value());
    request->send_P(200, "text/plain", "OK");
  });
  server.on("/motor/halt", HTTP_GET, [](AsyncWebServerRequest * request) {
    AsyncWebParameter* p = request->getParam("value");
    setMotorHalt(p->value());
    request->send_P(200, "text/plain", "OK");
  });
  server.on("/motor/speed", HTTP_GET, [](AsyncWebServerRequest * request) {
    AsyncWebParameter* p = request->getParam("value");
    setMotorSpeed(p->value());
    request->send_P(200, "text/plain", "OK");
  });
  server.on("/motor", HTTP_GET, [](AsyncWebServerRequest * request) {
    AsyncWebParameter* p = request->getParam("command");
    moveCommand(p->value());
    request->send_P(200, "text/plain", "OK");
  });
  server.on("/blink", HTTP_GET, [](AsyncWebServerRequest * request) {
    blinker = !blinker;
    if (!blinker) {
      setAllLightsOff();
    }
    Serial.print("Blinker: ");
    Serial.println(String(blinker));
    request->send_P(200, "text/plain", "OK");
  });

  server.begin();
}

void initLights() {
  for (int i = 0; i < 6; i++) {
    Serial.println("Setup PIN: " + String(lightPins[i]));
    pinMode(lightPins[i], OUTPUT);
  }
  setAllLightsOff();
}

void saveWifiCreds(String user, String pass) {
  Serial.println("Setting wifi ssid: " + user);
  pendForStore(0, user);
  pendForStore(20, pass);
  EEPROM.commit();
  Serial.println("Saved wifi creds");

  Serial.println("Restarting ...");
  ESP.restart();
}

void clearMemory(int pos, int size) {
  for (int i = pos; i < pos + size; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("Memory cleared!");
}

void pendForStore(int pos, String str) {
  for (int j = 0; j < str.length(); j++) {
    EEPROM.write(pos + j, str[j]);
  }
}

String readForStore(int pos, int size) {
  String val = "";
  for (int l = pos; l < pos + size; ++l) {
    int b = EEPROM.read(l);
    if (b > 0) {
      val += char(b);
    }
  }
  return val;
}

void setMotorHalt(String value) {
  motorHaltAngle = value.toInt();
  Serial.println("Motor halt set to: " + value);
  stopServo();
}

void setMotorSpeed(String value) {
  motorSpeedAngle = value.toInt();
  Serial.println("Motor speed set to: " + value);
  stopServo();
}

boolean servoEnabled = false;
void enableServo() {
  if (servoEnabled) return;

  servoEnabled = true;
  servo.attach(SERVO);
  Serial.println("Motor enabled");
}

void disableServo() {
  if (!servoEnabled) return;

  servoEnabled = false;
  servo.detach();
  Serial.println("Motor disabled");
}

void moveCommand(String command) {
  if (command == "forward") {
    moveForward();
  } else if (command == "back") {
    moveBackwards();
  } else {
    stopServo();
  }
}

boolean moveForward() {
  if (lastMoveDirection == 1) {
    return false;
  }
  enableServo();
  servo.write(motorHaltAngle + motorSpeedAngle);
  lastMoveCommandTime = millis();
  lastMoveDirection = 1;
  moveDuration = 0;
  timedMove = false;
  servoMoving = true;
  Serial.println("Motor forward");
  return true;
}

boolean moveForwardFor(int ms) {
  if (!moveForward()) {
    if (!beginTimedMove()) {
      return false;
    }
  }
  moveDuration = ms;
  return true;
}

boolean moveBackwardsFor(int ms) {
  if (!moveBackwards()) {
    if (!beginTimedMove()) {
      return false;
    }
  }
  moveDuration = ms;
  return true;
}

boolean moveBackwards() {
  if (lastMoveDirection == -1) {
    return false;
  }
  enableServo();
  servo.write(motorHaltAngle - motorSpeedAngle);
  lastMoveCommandTime = millis();
  lastMoveDirection = -1;
  moveDuration = 0;
  timedMove = false;
  servoMoving = true;
  Serial.println("Motor backward");
  return true;
}

boolean beginTimedMove() {
  boolean res;
  if (lastMoveDirection == 1) res = moveBackwardsFor(moveDuration);
  else if (lastMoveDirection == -1) res = moveForwardFor(moveDuration);
  timedMove = true;
  return res;
}

int points[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
const int pointsLen = 10;
boolean checkForAnomalies(int vout) {
  for (int i = 0; i < pointsLen - 1; i++) {
    points[i] = points[i + 1];
  }
  points[pointsLen - 1] = vout;

  double sumX = 0;
  double sumY = 0;
  double sumXY = 0;
  double sumXX = 0;
  for (int i = 0; i < pointsLen; i++) {
    double x = i * 10;
    sumX += x;
    sumY += points[i];
    sumXY += x * points[i];
    sumXX += x * x;
  }

  // b = (sum(x*y) - sum(x)sum(y)/n) / (sum(x^2) - sum(x)^2/n)
  double slope = (sumXY - sumX * sumY / pointsLen) / ( sumXX - sumX * sumX / pointsLen);
  double avgY = sumY / pointsLen;
  Serial.println(String(vout) + "\t" + String(slope));

  boolean res = (slope < -.3 || avgY < 860) && points[0] != 0 && points[pointsLen - 1] < 900;
  if (res) {
    Serial.println("Motor blocked!");
  }
  return res;
}

void stopServo() {
  servo.write(motorHaltAngle);
  disableServo();
  timedMove = false;
  servoMoving = false;
  for (int i = 0; i < pointsLen; i++) {
    points[i] = 0;
  }
  Serial.println("Motor stoped");
}

void commitLightLevels() {
  for (int i = 0; i < 6; i++) {
    analogWrite(lightPins[i], lightLevels[i]);
  }
}

void setAllLightsOff() {
  setAllLightsToLevel(0);
  commitLightLevels();
}

void setAllLightsToLevel(int level) {
  for (int i = 0; i < 6; i++) {
    lightLevels[i] = lightPwmStep * level;
  }
  Serial.println("All light channels set to level " + String(level));
}

void setLightChannelLevel(int index, int level) {
  if (index < 0 || index > 5) return;

  lightLevels[index] = lightPwmStep * level;
  Serial.println("Light channel " + String(index) + " set to level " + String(level));
}

void setLightChannelsLevel(int ch1, int ch2, int ch3, int ch4, int ch5, int ch6) {
  setLightChannelLevel(0, ch1);
  setLightChannelLevel(1, ch2);
  setLightChannelLevel(2, ch3);
  setLightChannelLevel(3, ch4);
  setLightChannelLevel(4, ch5);
  setLightChannelLevel(5, ch6);
}

void setLightChannelLevelAndCommitStr(String chan, String level) {
  if (chan == "" || level == "") {
    return;
  }
  setLightChannelLevelAndCommit(chan.toInt(), level.toInt());
}

void setLightChannelLevelAndCommit(int chan, int level) {
  setLightChannelLevel(chan, level);
  commitLightLevels();
}

void applyPreset(String name) {
  setAllLightsToLevel(0);
  commitLightLevels();

  boolean res;
  if (name == "sun") {
    setAllLightsToLevel(lightMaxLevel);
    res = moveForward();
  } else if (name == "sunset") {
    setLightChannelsLevel(lightMaxLevel, 0, lightMaxLevel, 0, lightMaxLevel, 0);
    res = moveForward();
  } else if (name == "fullmoon") {
    setLightChannelsLevel(0, 1, 0, 1, 0, 1);
    res = moveForward();
  } else if (name == "gibous") {
    setLightChannelsLevel(0, 2, 0, 0, 0, 0);
    res = moveBackwardsFor(1560);
  } else if ( name == "quarter") {
    setLightChannelsLevel(0, 2, 0, 0, 0, 0);
    res = moveBackwardsFor(780);
  } else if (name == "crescent") {
    setLightChannelsLevel(0, 1, 0, 0, 0, 0);
    res = moveBackwards();
  } else {
    res = moveBackwards();
  }

  if (!res) {
    commitLightLevels();
  }
}

void doTimedMove() {
  stopServo();
  if (moveDuration > 0) {
    beginTimedMove();
  } else {
    commitLightLevels();
  }
}

void endTimedMove() {
  stopServo();
  moveDuration = 0;
  commitLightLevels();
}

void readConsoleCommand() {
  String str = Serial.readStringUntil('\n');
  int cindex = str.indexOf(":");
  int vindex = str.indexOf(",");
  String cvalue[] = { "", "" };
  if (cindex >= 0) {
    if (vindex >= 0) {
      cvalue[0] = str.substring(cindex + 1, vindex);
      cvalue[1] = str.substring(vindex + 1);
    } else {
      cvalue[0] = str.substring(cindex + 1);
    }
  }

  Serial.println("Read serial command " + str + " with values " + cvalue[0] + " and " + cvalue[1]);

  if (str.startsWith("motor")) {
    moveCommand(cvalue[0]);
  } else if (str.startsWith("halt")) {
    setMotorHalt(cvalue[0]);
  } else if (str.startsWith("speed")) {
    setMotorSpeed(cvalue[0]);
  } else if (str.startsWith("light")) {
    setLightChannelLevelAndCommitStr(cvalue[0], cvalue[1]);
  } else if (str.startsWith("wifi")) {
    saveWifiCreds(cvalue[0], cvalue[1]);
  } else {
    applyPreset(str);
  }
}

void toggleStatusBlink() {
  setLightChannelLevelAndCommit(5, blinkState ? lightMaxLevel / 4 : 0);
  blinkState = !blinkState;
}

unsigned long previousMillis = 0;
unsigned long previousMillisBlinker = 0;
void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= tickInterval) {
    previousMillis = currentMillis;

    int vout = analogRead(A0);
    int dt = currentMillis - lastMoveCommandTime;
    if (servoMoving && dt > maxMoveTime) {
      Serial.println("Move timeout: vout " + String(vout));
      doTimedMove();
    } else if (servoMoving && checkForAnomalies(vout)) {
      doTimedMove();
    } else if (timedMove && moveDuration > 0 && dt >= moveDuration) {
      endTimedMove();
    }

    if (blinker) {
      if ((!blinkState && currentMillis - previousMillisBlinker > blinkInterval) || (blinkState && currentMillis - previousMillisBlinker > blinkInterval * 20)) {
        previousMillisBlinker = currentMillis;

        toggleStatusBlink();
      }
    }

    byte n = Serial.available();
    if (n != 0) {
      readConsoleCommand();
    }

  }
}