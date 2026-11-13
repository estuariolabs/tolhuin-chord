#pragma once
#include "Arduino.h"
struct NimBLEConnInfo {};
struct NimBLEAttValue {std::vector<uint8_t> v;const uint8_t* data(){return v.data();}size_t length(){return v.size();}};
class NimBLECharacteristic;
class NimBLEServer;
struct NimBLECharacteristicCallbacks {virtual void onWrite(NimBLECharacteristic*,NimBLEConnInfo&){}virtual ~NimBLECharacteristicCallbacks()=default;};
struct NimBLEServerCallbacks {virtual void onConnect(NimBLEServer*,NimBLEConnInfo&){}virtual void onDisconnect(NimBLEServer*,NimBLEConnInfo&,int){}virtual ~NimBLEServerCallbacks()=default;};
struct NimBLECharacteristic {NimBLEAttValue value;void setCallbacks(NimBLECharacteristicCallbacks*){}NimBLEAttValue getValue(){return value;}void setValue(const uint8_t*,size_t){}void notify(){}};
struct NimBLEService {NimBLECharacteristic* createCharacteristic(const char*,int){static NimBLECharacteristic c;return &c;}void start(){}const char* getUUID(){return "";}};
struct NimBLEServer {void setCallbacks(NimBLEServerCallbacks*){}NimBLEService* createService(const char*){static NimBLEService s;return &s;}};
struct NimBLEAdvertising {void addServiceUUID(const char*){}void start(){}};
struct NimBLEDevice {static void init(const char*){}static void setMTU(int){}static NimBLEServer* createServer(){static NimBLEServer s;return &s;}static NimBLEAdvertising* getAdvertising(){static NimBLEAdvertising a;return &a;}static void startAdvertising(){}};
namespace NIMBLE_PROPERTY {constexpr int READ=1,WRITE=2,WRITE_NR=4,NOTIFY=8;}
