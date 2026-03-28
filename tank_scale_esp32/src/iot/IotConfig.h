#pragma once
#include <Arduino.h>

// AWS IoT Core endpoint (Settings -> Data endpoint)
static const char* AWS_IOT_ENDPOINT = "a2ptav21b3qo1q-ats.iot.me-south-1.amazonaws.com";
static const int   AWS_IOT_PORT     = 8883;

// Your provisioning template name
static const char* PROV_TEMPLATE = "tanktelemetry-dev-template";

// Device identity: use a stable unique ID (mac, chip id, etc.)
inline String deviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[28];
  snprintf(buf, sizeof(buf), "mu-e32-%04X%08X",
           (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
}

// Topics
inline String topicUnclaimedTelemetry() {
  return String("unclaimed/") + deviceId() + "/telemetry";
}
inline String topicClaimedTelemetry(const String& tenantId) {
  return String("t/") + tenantId + "/d/" + deviceId() + "/telemetry";
}
inline String topicUnclaimedHeartbeat() {
  return String("unclaimed/") + deviceId() + "/heartbeat";
}
inline String topicClaimedHeartbeat(const String& tenantId) {
  return String("t/") + tenantId + "/d/" + deviceId() + "/heartbeat";
}
inline String topicUnclaimedEvent() {
  return String("unclaimed/") + deviceId() + "/event";
}
inline String topicClaimedEvent(const String& tenantId) {
  return String("t/") + tenantId + "/d/" + deviceId() + "/event";
}
