// =============================================
// BLE INTERFACE FOR THE PHONE APP
// =============================================
// One service, two characteristics (text lines, UTF-8):
//   TX (notify)  device -> app
//     D,ax,ay,az,gx,gy,gz,rawPitch,pitch,mode   live data (25 Hz), g and deg/s
//     B,adc,percent                             battery, every 2 s
//     S,key,value,min,max,type                  one settings table row (answer to GET)
//     S,END                                     end of settings list
//     C,step,PHASE[,value...]                   calibration progress
//         PHASE: START | WAIT,seconds | MEAS | DONE,value | NEXT | OK,offH,offVp,offVn | ERR,reason
//         NEXT = step n succeeded, device waits for "CAL NEXT" before step n+1
//   RX (write)   app -> device
//     GET            send settings table
//     SET key value  change a value (live, not yet stored)
//     SAVE           store settings to flash
//     RESET          restore default settings (calibration offsets are kept)
//     CAL START      start the 5-step calibration
//     CAL CANCEL     abort a running calibration
//     CAL NEXT       continue after a finished calibration step (answer to C,n,NEXT)

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define BLE_DEVICE_NAME  "LED-Waage"
#define BLE_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  // app writes here
#define BLE_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // device notifies here

BLECharacteristic* bleTx = nullptr;
volatile bool reqSettingsSend = false;
volatile bool reqSettingsSave = false;
volatile bool reqSettingsReset = false;

unsigned long bleLastStream = 0;
unsigned long bleLastBattery = 0;

void bleSend(const String& line) {
  if (!bleConnected || bleTx == nullptr) return;
  bleTx->setValue((uint8_t*)line.c_str(), line.length());
  bleTx->notify();
}

// Runs in the BLE task: only parse and set flags, never block here.
void bleHandleCommand(String cmd) {
  cmd.trim();
  if (cmd == "GET") {
    reqSettingsSend = true;
  } else if (cmd == "SAVE") {
    reqSettingsSave = true;
  } else if (cmd == "RESET") {
    reqSettingsReset = true;
  } else if (cmd == "CAL START") {
    if (appMode == APP_LEVEL) { calCancel = false; reqCalStart = true; }
    else bleSend("C,0,ERR,Not in level mode");
  } else if (cmd == "CAL CANCEL") {
    calCancel = true;
  } else if (cmd == "CAL NEXT") {
    reqCalNext = true;
  } else if (cmd.startsWith("SET ")) {
    int sp = cmd.indexOf(' ', 4);
    if (sp > 0) {
      Setting* s = settingFind(cmd.substring(4, sp));
      if (s) settingSet(s, cmd.substring(sp + 1).toFloat());
    }
  }
}

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) {
    auto v = c->getValue();
    bleHandleCommand(String(v.c_str()));
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) { bleConnected = true; }
  void onDisconnect(BLEServer*) {
    bleConnected = false;
    BLEDevice::startAdvertising();  // allow the app to reconnect
  }
};

void bleBegin() {
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setMTU(247);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(BLE_SERVICE_UUID);

  bleTx = service->createCharacteristic(BLE_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  bleTx->addDescriptor(new BLE2902());

  BLECharacteristic* rx = service->createCharacteristic(
      BLE_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCallbacks());

  service->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("[BLE] advertising as " BLE_DEVICE_NAME);
}

void bleSendSettings() {
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    Setting* s = &settingsTable[i];
    int dec = (s->type == 'f') ? 3 : 0;
    bleSend("S," + String(s->key) + "," + String(settingGet(s), dec) + "," +
            String(s->minV, dec) + "," + String(s->maxV, dec) + "," + String(s->type));
    delay(15);  // keep the notify queue from overflowing
  }
  bleSend("S,END");
}

// Called every loop(): handles queued requests and streams live data.
void bleService(unsigned long now, float ax, float ay, float az,
                float gx, float gy, float gz, float rawPitch) {
  if (reqSettingsReset) { reqSettingsReset = false; settingsReset(); reqSettingsSend = true; }
  if (reqSettingsSave)  { reqSettingsSave = false;  settingsSave(); }
  if (reqSettingsSend)  { reqSettingsSend = false;  bleSendSettings(); }

  if (!bleConnected) return;

  if (now - bleLastStream >= 40) {
    bleLastStream = now;
    int mode = (appMode == APP_SABER) ? 9 : (int)displayMode;  // 9 = lightsaber
    bleSend("D," + String(ax, 3) + "," + String(ay, 3) + "," + String(az, 3) + "," +
            String(gx, 1) + "," + String(gy, 1) + "," + String(gz, 1) + "," +
            String(rawPitch, 2) + "," + String(roundAngle(smoothedPitch), 2) + "," + String(mode));
  }
  if (now - bleLastBattery >= 2000) {
    bleLastBattery = now;
    int pct = batteryPercent();
    bleSend("B," + String(batteryAdcCached) + "," + String(pct));
  }
}
