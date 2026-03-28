#pragma once

#include <Arduino.h>
#include <Preferences.h>

struct ClaimManagerConfig {
  bool periodicPollingEnabled = true;                     // option to cancel 12h periodic polling
  uint32_t periodicPollMs = 12UL * 60UL * 60UL * 1000UL; // 12 hours
  uint32_t responseTimeoutMs = 30000;                    // 30s
  uint32_t retryMinMs = 15000;                           // 15s
  uint32_t retryMaxMs = 10UL * 60UL * 1000UL;            // 10 min
};

class ClaimManager {
public:
  void begin(const String& deviceId, const ClaimManagerConfig& cfg);
  void setConfig(const ClaimManagerConfig& cfg);

  const String& tenantId() const { return tenantId_; }
  bool isClaimed() const { return tenantId_.length() > 0; }

  const String& requestTopic() const { return requestTopic_; }
  const String& responseTopic() const { return responseTopic_; }

  void onMqttConnected(uint32_t nowMs);
  void onMqttDisconnected();
  bool isResponseSubscribed() const { return responseSubscribed_; }
  void markResponseSubscribed(bool v) { responseSubscribed_ = v; }

  // Called by app loop.
  void loop(uint32_t nowMs, bool mqttConnected);
  bool shouldPollNow(uint32_t nowMs, bool mqttConnected) const;
  String buildPollPayload() const;
  void notePollSent(uint32_t nowMs, bool sentOk);

  // Returns true if tenant assignment changed.
  bool handleClaimResponse(const String& topic, const String& payload, uint32_t nowMs);

private:
  void persistTenant_();
  void loadTenant_();
  void resetBackoff_();
  void growBackoff_();

  ClaimManagerConfig cfg_{};
  Preferences prefs_;
  bool prefsOpen_ = false;

  String deviceId_;
  String tenantId_;
  String requestTopic_;
  String responseTopic_;

  bool responseSubscribed_ = false;
  bool pendingImmediatePoll_ = true;
  bool awaitingResponse_ = false;
  uint32_t lastPollSentMs_ = 0;
  uint32_t nextPollNotBeforeMs_ = 0;
  uint32_t nextPeriodicPollMs_ = 0;
  uint32_t currentBackoffMs_ = 0;
};
