#pragma once
#include <Arduino.h>

struct IotIdentity {
  String deviceCertPem;   // PEM
  String devicePrivKeyPem;// PEM
  bool   hasIdentity = false;
};

class IotStore {
public:
  bool load(IotIdentity& out);
  bool save(const IotIdentity& in);
  void clear();

private:
  const char* CERT_PATH = "/iot/device.crt";
  const char* KEY_PATH  = "/iot/device.key";
};
