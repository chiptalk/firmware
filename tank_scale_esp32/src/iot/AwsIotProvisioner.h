#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "iot/IotStore.h"
#include "app/Log.h"

class AwsIotProvisioner {
public:
  using MessageHandler = void (*)(const String& topic, const String& payload, void* ctx);

  void begin(const char* rootCaPem,
             const char* claimCertPem,
             const char* claimKeyPem);

  // Call frequently in loop()
  void loop();

  bool isProvisioned() const { return provisioned_; }
  bool isReady() const { return mqttReady_; }

  // After provisioning, you call connectDeviceMqtt() to use device cert
  bool connectDeviceMqtt(const char* endpoint, int port, const char* clientId);

  // Publish helper
  bool publishJson(const String& topic, const JsonDocument& doc);
  bool publishRaw(const String& topic, const String& payload);
  bool subscribe(const String& topic);
  void setMessageHandler(MessageHandler cb, void* ctx) { msgHandler_ = cb; msgHandlerCtx_ = ctx; }
  void dispatchIncomingMessage(const String& topic, const String& payload);

  // Set when your backend "claims" and tells device tenantId (via your web portal or UART etc.)
  void setTenantId(const String& t) { tenantId_ = t; }
  const String& tenantId() const { return tenantId_; }
  uint32_t lastMqttTxMs() const { return lastMqttTxMs_; }
  uint32_t lastMqttRxMs() const { return lastMqttRxMs_; }

  // Expose last error/status for UI
  String status() const { return status_; }
  void clearIdentity() {
    store_.clear();
    ident_ = IotIdentity{};
    provisioned_ = false;
    mqttReady_ = false;
    status_ = "identity_cleared";
  }

  // Needed for initial provisioning step
  bool runProvisioning(const char* endpoint, int port, const String& deviceId, const char* templateName);

private:
  // MQTT
  WiFiClientSecure net_;
  PubSubClient mqtt_{net_};

  // Stored identity
  IotStore store_;
  IotIdentity ident_;

  // Claim creds (in flash)
  const char* rootCa_ = nullptr;
  const char* claimCert_ = nullptr;
  const char* claimKey_ = nullptr;

  // Device creds loaded/generated
  String status_ = "init";
  bool provisioned_ = false;
  bool mqttReady_ = false;
  String tenantId_;
  uint32_t lastMqttTxMs_ = 0;
  uint32_t lastMqttRxMs_ = 0;
  MessageHandler msgHandler_ = nullptr;
  void* msgHandlerCtx_ = nullptr;

  // Internal helpers
  void mqttLoop_();
};
