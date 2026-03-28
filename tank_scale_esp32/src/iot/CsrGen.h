#pragma once
#include <Arduino.h>

bool genEcKeyAndCsrPem(const String& commonName,
                       String& outPrivKeyPem,
                       String& outCsrPem);
