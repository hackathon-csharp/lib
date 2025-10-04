#include <Vector.h>
#include <datapacklib.h>
#include <string>
#include <iostream>

#include <WiFi.h>
#include <WebServer.h>

const int LED_BLUE = 23;
const int LED_GREEN = 22;
const int LED_RED = 21;

const char* ssid = "ESP_Super_criper_sigma_skuff"; 
const char* password = "12345678";
IPAddress local_ip(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);
// 192.168.4.1

WebServer server(80);

const int N = 1000, MX_MESSAGE_SIZE = 1000;
datapack::SignalChange storage_array[N];
Vector<datapack::SignalChange> v(storage_array);

uint8_t storage_message_array[MX_MESSAGE_SIZE];
Vector<uint8_t> messageBuffer(storage_message_array);

String JS = R"rawliteral(
  <script>
  async function setColor(color) {
    console.log(color);
    const response = await fetch('/setColor?' + new URLSearchParams(
      {
        color: color
      }
    ).toString());
  };

  async function send() {
    const mes = document.getElementById("message").value;
    console.log(mes);
    const response = await fetch('/send?' + new URLSearchParams(
      {
        msg: mes
      }
    ).toString());
  };
  </script>
)rawliteral";

String messages[20];  // Храним до 20 сообщений
int msgCount = 0;
std::string DebugColor = "";

// HTML страница с формой
String getHTMLPage() {
  String html = "<!DOCTYPE html><html><head><title>ESP Message Board</title></head><body>";
  html += "<h2>Send Message</h2>";
  // html += "<form action='/send' method='POST'>";

  html += "<input id=\"message\" onclick=send()/>";
  html += "<button onclick=send()>Off</button>";
  html += "</br>";
  html += "<button onclick=setColor(\"Green\")>Green</button>";
  html += "<button onclick=setColor(\"Red\")>Red</button>";
  html += "<button onclick=setColor(\"Blue\")>Blue</button>";
  html += "<button onclick=setColor(\"White\")>White</button>";
  html += "<button onclick=setColor(\"\")>Off</button>";

  html += "</body>";
  html += JS;
  html += "</html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", getHTMLPage());
}

int id = 0, lstTimer = 0;
void setMessage(const std::string& mes) {
  datapack::ProtocolConfig config;
  config.unitDurationMicros = 300;
  datapack::Encoder encoder(config);

  std::cout << mes << '\n';

  messageBuffer.clear();
  for (int i = 0; i < mes.length(); i++) {
    messageBuffer.push_back(mes[i]);
  }

  datapack::SignalBuffer encoded;
  encoder.encode(messageBuffer.data(), mes.length(), encoded);

  v.clear();
  std::cout << "size: " << encoded.size() << '\n';
  for (int i = 0; i < encoded.size(); i++) {
    v.push_back(encoded[i]);
    
    //v[i].duration *= 2;
    std::cout << "time: " << encoded[i].duration << " : ";
    std::cout << "level: ";
    if (encoded[i].level == datapack::LightLevel::Blue)
      std::cout << "Blue";
    if (encoded[i].level == datapack::LightLevel::Red)
      std::cout << "Red";
    if (encoded[i].level == datapack::LightLevel::Green)
      std::cout << "Green";
    if (encoded[i].level == datapack::LightLevel::White)
      std::cout << "White";
    if (encoded[i].level == datapack::LightLevel::Off)
      std::cout << "Off";
    std::cout << '\n';
  }
  v.push_back({datapack::LightLevel::Off, 15000});

  lstTimer = millis();
  id = 0;
  digitalWrite(LED_RED, 0);
  digitalWrite(LED_GREEN, 0);
  digitalWrite(LED_BLUE, 0);
}

void handleSend() {
  if (server.hasArg("msg")) {
    std::string msg = server.arg("msg").c_str();
    std::cout << msg << '\n';
    setMessage(msg);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void setColorHandle() {
  std::cout << "setColorHandle: " << server.args() << '\n';
  int size = server.args();
  for (int i = 0; i < size; i++) {
    std::cout << "arg: " << server.argName(i) << " : " << server.arg(i) << '\n';
  }
  if(server.hasArg("color")) {
    std::string color = server.arg("color").c_str();
    std::cout << color << '\n';
    DebugColor = color;
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

struct Command {
  int state;
  long time;
};

void initServer() {
  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(local_ip, gateway, subnet);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/send", HTTP_GET, handleSend);
  server.on("/setColor", HTTP_GET, setColorHandle);

  server.begin();
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("Connecting");
  Serial.println(WiFi.localIP());
}

void printData(const uint8_t* data, std::size_t length)
{
    std::cout << "Frame decoded successfully!\n";
    std::string utf8(reinterpret_cast<const char*>(data), length);
    std::cout << "Decoded payload (utf-8): " << utf8 << " size: " << length << '\n';
}

void decoderCallback(const uint8_t* data, std::size_t length, void* /*context*/) {
  std::cout << length << '\n';
  printData(data, length);
}

void setup() {
  Serial.begin(9600);

  while (!Serial) delay(1);
  Serial.print("Serial is ready!!!");

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  initServer();

  v.push_back({datapack::LightLevel::Off, 100000});
  // v.push_back({datapack::LightLevel::White, 5000});
  // v.push_back({datapack::LightLevel::Green, 5000});
  // v.push_back({datapack::LightLevel::Red, 5000});
  // v.push_back({datapack::LightLevel::Blue, 5000});

  lstTimer = millis();
}

void loop() {
  server.handleClient();
  int time = millis();
  
  if (DebugColor != "") {
    digitalWrite(LED_RED, 0);
    digitalWrite(LED_GREEN, 0);
    digitalWrite(LED_BLUE, 0);
    if (DebugColor == "Green") {
      digitalWrite(LED_GREEN, 255);
    }
    if (DebugColor == "Red") {
      digitalWrite(LED_RED, 255);
    }
    if (DebugColor == "Blue") {
      digitalWrite(LED_BLUE, 255);
    }
    if (DebugColor == "White") {
      digitalWrite(LED_RED, 255);
      digitalWrite(LED_BLUE, 255);
      digitalWrite(LED_GREEN, 255);
    }
    return;
  }

  if (lstTimer <= time) {
    lstTimer = time + v[id].duration;
    digitalWrite(LED_RED, 0);
    digitalWrite(LED_GREEN, 0);
    digitalWrite(LED_BLUE, 0);
    
    if (v[id].level == datapack::LightLevel::Blue) {
      digitalWrite(LED_BLUE, 255);
    }
    if (v[id].level == datapack::LightLevel::Red) {
      digitalWrite(LED_RED, 255);
    }
    if (v[id].level == datapack::LightLevel::Green) {
      digitalWrite(LED_GREEN, 255);
    }

    if (v[id].level == datapack::LightLevel::White) {
      digitalWrite(LED_GREEN, 255);
      digitalWrite(LED_RED, 255);
      digitalWrite(LED_BLUE, 255);
    }
    // Serial.print('\n');
    id = (id + 1) % v.size();
  }
}