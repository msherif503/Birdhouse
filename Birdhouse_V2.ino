#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h> // Use WiFiClient for HTTP, WiFiClientSecure for HTTPS
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Wi-Fi credentials
const char* ssid = "iotlab";
const char* password = "iotlab18";

// Server endpoints
const char* serverHost = "192.168.2.251"; // Replace with your server's host
const int serverPort = 8080;                 // Use 443 for HTTPS
const char* loginEndpoint = "/api/v1/user/login";
const char* uploadEndpoint = "/api/v1/image/add-image";

// Camera configuration pins (adjust according to your hardware)
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

// Global variables
String authToken = ""; // Authentication token

void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup...");

  // Connect to Wi-Fi
  Serial.print("Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");

  // Initialize the camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 10; // Adjust as needed (0-63 lower means better quality)
  config.fb_count = 1;

  // Initialize the camera
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera initialization failed");
    while (true); // Halt
  }
  Serial.println("Camera initialized.");

  // Authenticate with the server
  if (!authenticate()) {
    Serial.println("Authentication failed. Halting...");
    while (true); // Halt
  }
  Serial.println("Authentication successful.");
}

void loop() {
  Serial.println("Capturing image...");
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Image capture failed");
    delay(5000);
    return;
  }

  // Upload the image
  if (uploadImage(fb)) {
    Serial.println("Image uploaded successfully.");
  } else {
    Serial.println("Failed to upload image.");
  }

  // Return the frame buffer to the driver for reuse
  esp_camera_fb_return(fb);

  // Wait before capturing the next image
  delay(10000);
}

bool authenticate() {
  HTTPClient http;
  String url = String("http://") + serverHost + ":" + serverPort + loginEndpoint;
  http.begin(url);

  http.addHeader("Content-Type", "application/json");

  // Prepare JSON payload
  StaticJsonDocument<256> jsonDoc;
  jsonDoc["login"]["username"] = "admin";
  jsonDoc["login"]["password"] = "admin";
  jsonDoc["device"]["deviceName"] = "Device2";
  jsonDoc["device"]["deviceSchool"] = "School1";
  String requestBody;
  serializeJson(jsonDoc, requestBody);

  Serial.println("Sending authentication request...");
  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode == 200) {
    String response = http.getString();
    Serial.println("Authentication response:");
    Serial.println(response);

    StaticJsonDocument<512> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.f_str());
      http.end();
      return false;
    }

    // Extract token based on actual response structure
    if (responseDoc.containsKey("token")) {
      authToken = responseDoc["token"].as<String>();
    } else if (responseDoc["data"]["token"]) {
      authToken = responseDoc["data"]["token"].as<String>();
    } else {
      Serial.println("Token not found in authentication response.");
      http.end();
      return false;
    }

    Serial.print("Received token: ");
    Serial.println(authToken);
    http.end();
    return true;
  } else {
    Serial.print("Authentication failed, HTTP response code: ");
    Serial.println(httpResponseCode);
    String errorResponse = http.getString();
    Serial.println("Error response:");
    Serial.println(errorResponse);
    http.end();
    return false;
  }
}

bool uploadImage(camera_fb_t *fb) {
  WiFiClient client;
  if (!client.connect(serverHost, serverPort)) {
    Serial.println("Connection to server failed");
    return false;
  }

  // Generate boundary string
  String boundary = "------------------------" + String(millis());

  // Prepare HTTP request
  String head = "--" + boundary + "\r\n" +
                "Content-Disposition: form-data; name=\"image\"; filename=\"photo.jpg\"\r\n" +
                "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";

  // Calculate content length
  size_t contentLength = head.length() + fb->len + tail.length();

  // Send HTTP POST request
  client.print("POST " + String(uploadEndpoint) + " HTTP/1.1\r\n");
  client.print("Host: " + String(serverHost) + "\r\n");
  client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
  client.print("authorization: " + authToken + "\r\n");
  client.print("Content-Length: " + String(contentLength) + "\r\n");
  client.print("Connection: close\r\n\r\n");

  // Send HTTP body
  client.print(head);
  client.write(fb->buf, fb->len);
  client.print(tail);

  Serial.println("Image data sent. Waiting for server response...");

  // Read server response
  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 10000) {
      Serial.println("Server response timed out");
      client.stop();
      return false;
    }
  }

  // Read and print the response
  String response = client.readString();
  Serial.println("Server response:");
  Serial.println(response);

  client.stop();

  // Check for success
  if (response.indexOf("200 OK") != -1 || response.indexOf("\"status\":\"success\"") != -1) {
    return true;
  } else {
    return false;
  }
}
