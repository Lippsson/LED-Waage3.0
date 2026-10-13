#pragma once
// Table row for a runtime-editable setting (see AppSettings.ino)
struct Setting {
  const char* key;   // also the NVS key (max 15 chars)
  char        type;  // 'f' float, 'i' int
  void*       ptr;
  float       minV;
  float       maxV;
  float       defV;
};

