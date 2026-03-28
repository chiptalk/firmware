// src/iot/AwsIotProvisioner.cpp

#include "AwsIotProvisioner.h"
#include "iot/IotConfig.h"
#include <ArduinoJson.h>
#include "app/Log.h"

// Must be big enough to hold create/json accepted payload (cert + privateKey)
static constexpr size_t MQTT_BUF_SIZE = 8192;
static volatile uint32_t g_lastMqttRxActivityMs = 0;
static AwsIotProvisioner* g_deviceMqttOwner = nullptr;

#ifndef MQTT_DEBUG_MODE
#define MQTT_DEBUG_MODE 0
#endif

static void deviceMqttCb(char* topic, byte* payload, unsigned int len) {
  g_lastMqttRxActivityMs = millis();
  if (!g_deviceMqttOwner) return;

  String t(topic);
  String p;
  p.reserve(len + 1);
  for (unsigned int i = 0; i < len; i++) p += (char)payload[i];

  g_deviceMqttOwner->dispatchIncomingMessage(t, p);
}

// ---------- lifecycle ----------

void AwsIotProvisioner::begin(const char* rootCaPem,
                             const char* claimCertPem,
                             const char* claimKeyPem) {
  rootCa_ = rootCaPem;
  claimCert_ = claimCertPem;
  claimKey_ = claimKeyPem;

  provisioned_ = store_.load(ident_);
  status_ = provisioned_ ? "device_identity_loaded" : "needs_provisioning";
}

bool AwsIotProvisioner::connectDeviceMqtt(const char* endpoint, int port, const char* clientId) {
  if (!store_.load(ident_)) {
    status_ = "no_device_identity";
    return false;
  }

#if MQTT_DEBUG_MODE
  Log::i("AWS IoT connect (DEVICE) details");
  Log::i(String(" endpoint=") + endpoint);
  Log::i(String(" port=") + port);
  Log::i(String(" clientId=") + clientId);
  Log::i(String(" rootCA len=") + (rootCa_ ? (int)strlen(rootCa_) : 0));
  Log::i(String(" deviceCert len=") + ident_.deviceCertPem.length());
  Log::i(String(" deviceKey len=") + ident_.devicePrivKeyPem.length());
#endif

  net_.setCACert(rootCa_);
  net_.setCertificate(ident_.deviceCertPem.c_str());
  net_.setPrivateKey(ident_.devicePrivKeyPem.c_str());
  net_.setHandshakeTimeout(30);

  mqtt_.setServer(endpoint, port);
  mqtt_.setBufferSize(MQTT_BUF_SIZE);
  g_deviceMqttOwner = this;
  mqtt_.setCallback(deviceMqttCb);

  mqttReady_ = mqtt_.connect(clientId);
  if (mqttReady_) {
    status_ = "mqtt_connected_device";
  } else {
    const int st = mqtt_.state();
    status_ = String("mqtt_connect_failed_device:state=") + st;
#if MQTT_DEBUG_MODE
    Log::e(String("DEVICE MQTT connect failed, PubSubClient state=") + st);
#endif
  }
  return mqttReady_;
}

void AwsIotProvisioner::mqttLoop_() {
  if (!mqttReady_) return;
  mqtt_.loop();
  if (!mqtt_.connected()) {
    mqttReady_ = false;
    status_ = String("mqtt_disconnected:state=") + mqtt_.state();
#if MQTT_DEBUG_MODE
    Log::w(String("MQTT disconnected, PubSubClient state=") + mqtt_.state());
#endif
  }
}

bool AwsIotProvisioner::publishJson(const String& topic, const JsonDocument& doc) {
  if (!mqttReady_ || !mqtt_.connected()) {
    mqttReady_ = false;
    status_ = String("mqtt_not_connected:state=") + mqtt_.state();
    return false;
  }
  String payload;
  serializeJson(doc, payload);
  bool ok = mqtt_.publish(topic.c_str(), payload.c_str());
  if (ok) lastMqttTxMs_ = millis();
  else if (!mqtt_.connected()) {
    mqttReady_ = false;
    status_ = String("mqtt_publish_failed_disconnected:state=") + mqtt_.state();
  }
  return ok;
}

bool AwsIotProvisioner::publishRaw(const String& topic, const String& payload) {
  if (!mqttReady_ || !mqtt_.connected()) {
    mqttReady_ = false;
    status_ = String("mqtt_not_connected:state=") + mqtt_.state();
    return false;
  }
  bool ok = mqtt_.publish(topic.c_str(), payload.c_str());
  if (ok) lastMqttTxMs_ = millis();
  else if (!mqtt_.connected()) {
    mqttReady_ = false;
    status_ = String("mqtt_publish_failed_disconnected:state=") + mqtt_.state();
  }
  return ok;
}

bool AwsIotProvisioner::subscribe(const String& topic) {
  if (!mqttReady_ || !mqtt_.connected()) return false;
  return mqtt_.subscribe(topic.c_str());
}

void AwsIotProvisioner::dispatchIncomingMessage(const String& topic, const String& payload) {
  if (!msgHandler_) return;
  msgHandler_(topic, payload, msgHandlerCtx_);
}

void AwsIotProvisioner::loop() {
  if (g_lastMqttRxActivityMs > lastMqttRxMs_) lastMqttRxMs_ = g_lastMqttRxActivityMs;
  mqttLoop_();
}

// ---------- provisioning helpers ----------

static String g_lastTopic;
static String g_lastPayload;

static void mqttCb(char* topic, byte* payload, unsigned int len) {
  g_lastTopic = String(topic);
  g_lastPayload = "";
  g_lastPayload.reserve(len + 1);
  for (unsigned int i = 0; i < len; i++) g_lastPayload += (char)payload[i];
  g_lastMqttRxActivityMs = millis();
}

static bool waitForAnyTopicWithPrefix(PubSubClient& mqtt, const String& prefix, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    mqtt.loop();
    delay(5);
    if (g_lastTopic.startsWith(prefix)) return true;
  }
  return false;
}

// ---------- provisioning (CreateKeysAndCertificate + Fleet Provisioning) ----------

bool AwsIotProvisioner::runProvisioning(const char* endpoint, int port,
                                        const String& deviceId,
                                        const char* templateName) {
  String privKeyPem;
  String certificatePem;
  String ownershipToken;

  // ---- helper: reset last message
  auto resetLast = []() {
    g_lastTopic = "";
    g_lastPayload = "";
  };

  // ---- helper: wait for message on any prefix (robust)
  static volatile uint32_t g_msgCount = 0;

  // Wrap your callback to bump a counter too (safe enough for this use)
  // NOTE: g_msgCount must be incremented inside callback, so we re-set callback below.
  auto setCountingCallback = [&]() {
    mqtt_.setCallback([](char* topic, byte* payload, unsigned int len) {
      g_lastTopic = String(topic);
      g_lastPayload = "";
      g_lastPayload.reserve(len + 1);
      for (unsigned int i = 0; i < len; i++) g_lastPayload += (char)payload[i];
      g_msgCount++;
      g_lastMqttRxActivityMs = millis();
    });
  };

  auto waitForPrefix = [&](const String& prefix, uint32_t timeoutMs) -> bool {
    const uint32_t start = millis();
    const uint32_t startCount = g_msgCount;
    while (millis() - start < timeoutMs) {
      mqtt_.loop();
      delay(5);
      if (g_msgCount != startCount && g_lastTopic.startsWith(prefix)) return true;
    }
    return false;
  };

  // Connect with CLAIM cert
  net_.setCACert(rootCa_);
  net_.setCertificate(claimCert_);
  net_.setPrivateKey(claimKey_);
  net_.setHandshakeTimeout(30);

  mqtt_.setServer(endpoint, port);
  mqtt_.setBufferSize(MQTT_BUF_SIZE);
  setCountingCallback();

  String claimClientId = "claim-" + deviceId;

  Log::i("AWS IoT connect (CLAIM)");
  Log::i(String(" endpoint=") + endpoint);
  Log::i(String(" port=") + port);
  Log::i(String(" clientId=") + claimClientId);
  Log::i(String(" rootCA len=") + strlen(rootCa_));
  Log::i(String(" claimCert len=") + strlen(claimCert_));
  Log::i(String(" claimKey len=") + strlen(claimKey_));
  Log::i(String(" mqtt buf=") + (int)MQTT_BUF_SIZE);

  if (!mqtt_.connect(claimClientId.c_str())) {
    status_ = "mqtt_connect_failed_claim";
    return false;
  }

  // -------------------------
  // IMPORTANT: AWS RESERVED TOPICS (NO "/+")
  // -------------------------
  const String createAcceptedTopic = "$aws/certificates/create/json/accepted";
  const String createRejectedTopic = "$aws/certificates/create/json/rejected";

  const String provAcceptedTopic =
      String("$aws/provisioning-templates/") + templateName + "/provision/json/accepted";
  const String provRejectedTopic =
      String("$aws/provisioning-templates/") + templateName + "/provision/json/rejected";

  // Subscribe (check return values!)
  bool s1 = mqtt_.subscribe(createAcceptedTopic.c_str());
  bool s2 = mqtt_.subscribe(createRejectedTopic.c_str());
  bool s3 = mqtt_.subscribe(provAcceptedTopic.c_str());
  bool s4 = mqtt_.subscribe(provRejectedTopic.c_str());

  if (!s1 || !s2 || !s3 || !s4) {
    status_ = "subscribe_failed";
    mqtt_.disconnect();
    return false;
  }

  // 1) Create keys + cert (no CSR)
  resetLast();
  const uint32_t beforeCreateCount = g_msgCount;

  if (!mqtt_.publish("$aws/certificates/create/json", "{}")) {
    status_ = "publish_create_cert_failed";
    mqtt_.disconnect();
    return false;
  }
  lastMqttTxMs_ = millis();

  // Wait for either accepted or rejected
  // (We wait on common prefix, then verify exact topic)
  if (!waitForPrefix("$aws/certificates/create/json", 15000)) {
    status_ = "create_cert_timeout";
    mqtt_.disconnect();
    return false;
  }

  // If some other message arrived first, keep waiting until timeout for either accepted/rejected
  {
    const uint32_t start = millis();
    while (millis() - start < 15000) {
      mqtt_.loop();
      delay(5);

      if (g_msgCount == beforeCreateCount) continue; // no new messages yet

      if (g_lastTopic == createRejectedTopic) {
        status_ = "create_cert_rejected:" + g_lastPayload;
        mqtt_.disconnect();
        return false;
      }
      if (g_lastTopic == createAcceptedTopic) break;

      // Not the message we need; keep waiting (don’t reset, just continue)
    }

    if (g_lastTopic != createAcceptedTopic) {
      status_ = "create_cert_timeout";
      mqtt_.disconnect();
      return false;
    }
  }

  // Parse create cert response
  {
    DynamicJsonDocument resp(8192);
    auto err = deserializeJson(resp, g_lastPayload);
    if (err) {
      status_ = "create_cert_resp_parse_failed";
      mqtt_.disconnect();
      return false;
    }

    ownershipToken = resp["certificateOwnershipToken"].as<String>();
    certificatePem = resp["certificatePem"].as<String>();
    privKeyPem     = resp["privateKey"].as<String>();

    if (!ownershipToken.length() || !certificatePem.length() || !privKeyPem.length()) {
      status_ = "create_cert_resp_missing_fields";
      mqtt_.disconnect();
      return false;
    }
  }

  // 2) Provision using template
  {
    DynamicJsonDocument doc2(1024);
    doc2["certificateOwnershipToken"] = ownershipToken;
    JsonObject params = doc2.createNestedObject("parameters");
    params["DeviceId"] = deviceId;

    String payload;
    serializeJson(doc2, payload);

    resetLast();
    const uint32_t beforeProvCount = g_msgCount;

    const String provPublishTopic =
        String("$aws/provisioning-templates/") + templateName + "/provision/json";

    if (!mqtt_.publish(provPublishTopic.c_str(), payload.c_str())) {
      status_ = "publish_prov_failed";
      mqtt_.disconnect();
      return false;
    }
    lastMqttTxMs_ = millis();

    // Wait for accepted or rejected for provisioning
    const uint32_t start = millis();
    while (millis() - start < 15000) {
      mqtt_.loop();
      delay(5);
      if (g_msgCount == beforeProvCount) continue;

      if (g_lastTopic == provRejectedTopic) {
        status_ = "prov_rejected:" + g_lastPayload;
        mqtt_.disconnect();
        return false;
      }
      if (g_lastTopic == provAcceptedTopic) break;
      // otherwise keep waiting
    }

    if (g_lastTopic != provAcceptedTopic) {
      status_ = "prov_timeout";
      mqtt_.disconnect();
      return false;
    }
  }

  // Save device identity locally
  IotIdentity id;
  id.deviceCertPem = certificatePem;
  id.devicePrivKeyPem = privKeyPem;

  if (!store_.save(id)) {
    status_ = "save_identity_failed";
    mqtt_.disconnect();
    return false;
  }

  status_ = "provisioned_saved";
  provisioned_ = true;

  mqtt_.disconnect();
  mqttReady_ = false;
  return true;
}
