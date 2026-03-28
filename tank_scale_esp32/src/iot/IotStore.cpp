#include "IotStore.h"
#include <LittleFS.h>

static String readFile(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}
static bool writeFile(const char* path, const String& s) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  size_t n = f.print(s);
  f.close();
  return n == (size_t)s.length();
}

bool IotStore::load(IotIdentity& out) {
  out.deviceCertPem = readFile(CERT_PATH);
  out.devicePrivKeyPem = readFile(KEY_PATH);
  out.hasIdentity = out.deviceCertPem.length() > 0 && out.devicePrivKeyPem.length() > 0;
  return out.hasIdentity;
}

bool IotStore::save(const IotIdentity& in) {
  LittleFS.mkdir("/iot");
  return writeFile(CERT_PATH, in.deviceCertPem) && writeFile(KEY_PATH, in.devicePrivKeyPem);
}

void IotStore::clear() {
  LittleFS.remove(CERT_PATH);
  LittleFS.remove(KEY_PATH);
}
