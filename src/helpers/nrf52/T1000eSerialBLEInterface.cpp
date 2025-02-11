#include "T1000eSerialBLEInterface.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static T1000eSerialBLEInterface * _theOne;

void T1000eSerialBLEInterface::begin(const char* device_name, uint32_t pin_code) {
  _pin_code = pin_code;
  char charpin[20];
  sprintf(charpin, "%d", _pin_code);

  _theOne = this;

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

  Bluefruit.begin(1, 0); // 1 periph, 1 central
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values
  Bluefruit.setName("MeshCore"); // useful testing with multiple central connections
  Bluefruit.Security.setPIN(charpin);

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  bledfu.begin();

  // Configure and Start Device Information Service
  bledis.setManufacturer("MeshCore");
  bledis.setModel("T1000e");
  bledis.begin();

//  bleuart.bufferTXD(true);
  bleuart.begin();
  bleuart.setRxCallback(rx_callback);

  startAdv();

}

void T1000eSerialBLEInterface::startAdv(void)
{
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

  // Include bleuart 128-bit uuid
  Bluefruit.Advertising.addService(bleuart);

  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  Bluefruit.ScanResponse.addName();
  
  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds  
}

// ---------- public methods

void T1000eSerialBLEInterface::enable() { 
  if (_isEnabled) return;

  _isEnabled = true;
}

void T1000eSerialBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("T1000eSerialBLEInterface::disable");
}

size_t T1000eSerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

#define  BLE_WRITE_MIN_INTERVAL   20

bool T1000eSerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;   // still too soon to start another write?
}

size_t T1000eSerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  if (!deviceConnected) {
    return 0;
  }

  if (send_queue_len > 0   // first, check send queue
    && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL    // space the writes apart
  ) {
    _last_write = millis();
    bleuart.write(send_queue[0].buf, send_queue[0].len);
    //pTxCharacteristic->setValue(send_queue[0].buf, send_queue[0].len);
    //pTxCharacteristic->notify();

    BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
      send_queue[i] = send_queue[i + 1];
    }
  }

  if (recv_queue_len > 0) {   // check recv queue
    size_t len = recv_queue[0].len;   // take from top of queue
    memcpy(dest, recv_queue[0].buf, len);

    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", len, (uint32_t) dest[0]);

    recv_queue_len--;
    for (int i = 0; i < recv_queue_len; i++) {   // delete top item from queue
      recv_queue[i] = recv_queue[i + 1];
    }
    return len;
  }
  return 0;
}

bool T1000eSerialBLEInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}

// Serial callbacks

void T1000eSerialBLEInterface::onWrite(uint16_t conn_hdl) {
  int len;
  len = bleuart.read(recv_queue[recv_queue_len].buf, MAX_FRAME_SIZE);
  recv_queue[recv_queue_len].len = len;
  recv_queue_len++;
}

void T1000eSerialBLEInterface::rx_callback(uint16_t conn_hdl) {
  _theOne->onWrite(conn_hdl);
  BLE_DEBUG_PRINTLN("RECV");
}

void T1000eSerialBLEInterface::connect_callback(uint16_t conn_hdl){
  _theOne->deviceConnected = true;
  _theOne->clearBuffers();
  BLE_DEBUG_PRINTLN("CX");
}
void T1000eSerialBLEInterface::disconnect_callback(uint16_t conn_hdl, uint8_t reason){
  _theOne->deviceConnected = false;
  BLE_DEBUG_PRINTLN("DCX");
}