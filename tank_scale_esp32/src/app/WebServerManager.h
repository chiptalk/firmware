/*
#pragma once
#include <Arduino.h>
#include <memory>
#include <WebServer.h>
#include <DNSServer.h>
#include "WifiManager.h"
#include "AppState.h"

class WebServerManager {
public:
  void beginApPortal(WifiManager& wifi, AppState& state);
  void beginStaServer(WifiManager& wifi, AppState& state);
  void loop();

private:
  std::unique_ptr<WebServer> server_;
  DNSServer dns_;
  bool apPortal_ = false;

  WifiManager* wifi_ = nullptr;
  AppState* state_ = nullptr;

  // Basic runtime settings (example)
  int paramA_ = 42;

  void resetServer_();
  void setupRoutesAp_();
  void setupRoutesSta_();

  // Captive portal helpers
  void addCaptivePortalRoutes_();
  void sendCaptiveRedirect_();

  String htmlPortal_() const;
  String htmlHome_() const;
};
*/
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

#include "WifiManager.h"
#include "AppState.h"
#include "TankTelemetry.h"

class WebServerManager {
public:
  WebServerManager() = default;
  ~WebServerManager();

  void beginApPortal(WifiManager& wifi, AppState& state);
  void beginStaServer(WifiManager& wifi, AppState& state);
  void loop();
  void attachTelemetryConfig(TankTelemetryConfig& cfg) { telemetryCfg_ = &cfg; }

private:
  WebServer* server_ = nullptr;
  DNSServer dns_;
  bool apPortal_ = false;

  WifiManager* wifi_ = nullptr;
  AppState* state_ = nullptr;
  TankTelemetryConfig* telemetryCfg_ = nullptr;

  // Example runtime parameter
  int paramA_ = 42;

  // Core
  void resetServer_();
  void setupRoutesAp_();
  void setupRoutesSta_();

  // Captive portal
  void addCaptivePortalRoutes_();
  void sendCaptiveRedirect_();

  // Helpers
  void serveFile_(const char* path, const char* contentType);
  void markCommRx_();
  void markCommTx_();

  // Pages
  String htmlPortal_() const;
  String htmlHome_() const;
};
