/* ESP32 TWAI Canbus monitor

  Connect a CAN bus transceiver to the RX/TX pins.   For example: SN65HVD230

  The API gives other possible speeds and alerts:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html

  created 27-06-2023 by Stephan Martin (designer2k2)
*/

//#define CHECK_SEQ_NUMBERS   // uncomment for data[0] sequence number check

#include "driver/twai.h"

// Pins used to connect to CAN bus transceiver:
#define CAN_TX 17
#define CAN_RX 16

#define POLLING_RATE_MS 1000
uint32_t mask = 0, filter = 0, extended = 0;  // defdault mask and filter
// test to filter out CanIDs 0x120 to 0x127
//uint32_t mask = 0x7F8,  filter=  0x120, extended = 0;
//uint32_t mask = 0x1FFFFFF8, filter = 0x120, extended = 1;

twai_timing_config_t config = TWAI_TIMING_CONFIG_125KBITS();  // defuult

static bool driver_installed = false;

void setup() {
  // Start Serial:
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\nESP32 Canbus Monitor V1.0");
  setupCanbus(false, TWAI_TIMING_CONFIG_125KBITS(), extended, mask, filter);  // setup TWAI canbus interface
  displayMenu();        // display the control menu                 
}

// initialise the TWAI Canbus interface
void setupCanbus(bool stopDriver, twai_timing_config_t config, uint32_t extended, uint32_t mask, uint32_t filter) {
  Serial.println("\n\nESP32 Canbus Monitor V1.0");
  Serial.print("Configuring TWAI Canbus ");
  if (stopDriver) {                   // driver already open? close it?
    // stop and close driver
    if (twai_stop() == ESP_OK) {
      Serial.print("Driver stoped ");
    } else {
      Serial.println("Failed to stop driver");
      return;
    }
    if (twai_driver_uninstall() == ESP_OK) {
      Serial.println("Driver uninstalled OK");
    } else {
      Serial.println("ERROR! Driver uninstalled failed!");
      return;
    }
  }
  //Serial.printf(" extended %d Mask 0x%x Filter 0x%x\n", extended, mask, filter);
  // Initialize configuration structures using macro initializers
  twai_general_config_t g_config = { 0, TWAI_MODE_NORMAL, (gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX,
                                     (gpio_num_t)-1, (gpio_num_t)-1, 5, 50 };
  twai_timing_config_t t_config = config;  //TWAI_TIMING_CONFIG_125KBITS();   // setup baudrate
  twai_filter_config_t f_config;                                              // setup mask and filter
  if (mask == 0 && filter == 0) f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();   // no mask and filter
  else {
    if (extended) {
      f_config.acceptance_code = (filter << 3);  // setup extended 29 bit mask and filter
      f_config.acceptance_mask = ~(mask << 3);
    } else {
      f_config.acceptance_code = (filter << 21);  // setup standard 11 bit mask and filter
      f_config.acceptance_mask = ~(mask << 21);
    }
    f_config.single_filter = true;
  }
  // Install TWAI driver
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.print("Driver installed ");
  } else {
    Serial.println("Failed to install driver");
    return;
  }
  // Start TWAI driver
  if (twai_start() == ESP_OK) {
    Serial.print("Driver started ");
  } else {
    Serial.println("Failed to start driver");
    return;
  }

  // Reconfigure alerts to detect TX alerts and Bus-Off errors
  uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS
                              | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK) {
    Serial.println("- CAN Alerts reconfigured");
  } else {
    Serial.println("Failed to reconfigure alerts");
    return;
  }
  driver_installed = true;  // TWAI driver is now successfully installed and started
}

// loop checking for TWAI alerts, transmit/receive frames etc
void loop() {
  if (!driver_installed) {
    // Driver not installed
    delay(1000);
    return;
  }
  // Check if alert happened
  uint32_t alerts_triggered;
  twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(POLLING_RATE_MS));
  twai_status_info_t twaistatus;
  twai_get_status_info(&twaistatus);
  // Handle alerts
  if (alerts_triggered & TWAI_ALERT_ERR_PASS) {
    Serial.println("Alert: TWAI controller has become error passive.");
  }
  if (alerts_triggered & TWAI_ALERT_BUS_ERROR) {
    Serial.println("Alert: A (Bit, Stuff, CRC, Form, ACK) error has occurred on the bus.");
    Serial.printf("Bus error count: %lu\n", twaistatus.bus_error_count);
  }
  if (alerts_triggered & TWAI_ALERT_TX_FAILED) {
    Serial.println("Alert: The Transmission failed.");
    Serial.printf("TX buffered: %lu\t", twaistatus.msgs_to_tx);
    Serial.printf("TX error: %lu\t", twaistatus.tx_error_counter);
    Serial.printf("TX failed: %lu\n", twaistatus.tx_failed_count);
  }
  if (alerts_triggered & TWAI_ALERT_TX_SUCCESS) {
    Serial.print("Alert: The Transmission was successful.");
    Serial.printf(":  TX buffered: %lu\t\n", twaistatus.msgs_to_tx);
  }
  // receiver
  if (alerts_triggered & TWAI_ALERT_RX_QUEUE_FULL) {
    Serial.println("Alert: The RX queue is full causing a received frame to be lost.");
    Serial.printf("RX buffered: %lu\t", twaistatus.msgs_to_rx);
    Serial.printf("RX missed: %lu\t", twaistatus.rx_missed_count);
    Serial.printf("RX overrun %lu\n", twaistatus.rx_overrun_count);
  }
  // Check if message is received
  if (alerts_triggered & TWAI_ALERT_RX_DATA) {
    // One or more messages received. Handle all.
    twai_message_t message;
    while (twai_receive(&message, 0) == ESP_OK) {
      handle_rx_message(message);
    }
  }
  // on keyboard hit check for commands
  if (Serial.available()) checkMenuCommand();
}

// display menu of commands
void displayMenu(void) {
  Serial.println("\nCanbus commands (? this menu) \n"
                 " I - ReInitialise TWAI Canbus interface\n"
                 " B - setup Canbus baud rate\n"
                 " M - setup Mask and Filter\n"
                 " T - transmit a data frame\n"
                 " R - transmit a RTR frame\n");
}

// character entered check if in menu of commands
void checkMenuCommand(void) {
  // > is from C# canbus monitor to initialise TWAI interface etc
  char ch = command("Enter copmmand ? ", ">IBMTR?", false);
  switch (ch) {
    case '>':
    case 'I':
      config = TWAI_TIMING_CONFIG_125KBITS();
      extended = mask = filter = 0;
      setupCanbus(true, config, extended, mask, filter);
      break;
    case 'B':
      setupBaudRate();
      break;
    case 'M':
      setupMaskFilter();
      break;
    case 'T':
      serialToCanTx(false);  // Send message
      break;
    case 'R':
      serialToCanTx(true);  // Send message
      break;
    case '?':
      displayMenu();
      break;
    default:
      Serial.println("illegal command!");
  }
}

// read Serial stream flusing control characters
void flushControlCharacters(void) {
  while (Serial.available())
    if (Serial.peek() < ' ') Serial.read();
    else break;
}

// display question - read character - if in commands[] return
int command(char *question, char *commands, bool flushSerial) {
  Serial.setTimeout(100000);
  flushControlCharacters();
  if (!Serial.available())  Serial.print(question); // if not input ask question
  int ch = ' ';
  while ((ch = Serial.read()) == -1) delay(10);     // wait for character entered
  flushControlCharacters();
  //while (Serial.available()) Serial.read();      // clear read buffer
  if (ch >= ' ') Serial.print(char(ch));  // echo character
  //if (ch == 0x1b) return 0x1b;
  for (int i = 0; i < strlen(commands); i++)             // check command
    if (toupper(ch) == commands[i]) return toupper(ch);  // found
  displayMenu();
  return 0x1b;  // return fail!
}

// enter CanID 11  or 29 bits - display question check maximum value
uint32_t readCanID(char *question, const int32_t maximum) {
  Serial.setTimeout(100000);
  while (1) {
    flushControlCharacters();
    if (!Serial.available())  Serial.print(question);
    char text[50] = { 0 };
    Serial.readBytesUntil('\n', text, 50);  // read string
    flushControlCharacters();
    Serial.printf("%s\n", text);
    uint32_t canID = 0;
    uint8_t data_len = sscanf(text, "%x", &canID);                                                  // parse one HEX value
    if (data_len != 1) Serial.println("invalid HEX value - try again");                             // one value converted OK?
    else if (canID > maximum) Serial.printf(" 0x%X>0x%X too large - try again\n", canID, maximum);  // check maximum error
    else return canID;                                                                              // all OK return HEX value
  }
}

void setupMaskFilter(void) {
  if(!Serial.available()) Serial.print("\nSetup Canbus mask and filter\n");
  char ch;
  // select standard or extended CanID and read value
  if ((ch = command(" - enter S - Standard or E - Extended CanID ? ", "SE", true)) <= ' ') return;
  uint32_t canID;
  switch (toupper(ch)) {
    case 'S':
      extended = 0;
      mask = readCanID("\nenter standard 11-bit mask (0 to 0x7FF) ? ", 0x7FF);
      filter = readCanID("enter standard 11-bit filter (0 to 0x7FF) ? ", 0x7FF);
      break;
    case 'E':
      extended = 1;
      mask = readCanID("\nenter Extended 29-bit mask (0 to 0x1FFFFFFF) ? ", 0x1FFFFFFF);
      filter = readCanID("enter Extended 29-bit filter (0 to 0x1FFFFFFF) ? ", 0x1FFFFFFF);
      break;
  }
  if (extended)
    Serial.printf("Mask EXT 0x%x filter 0x%x\n", mask, filter);
  else
    Serial.printf("Mask STD 0x%x filter 0x%x\n", mask, filter);
  setupCanbus(true, config, extended, mask, filter);
  //Serial.println("Mask and Filter updated");
}

// setup baudrate
void setupBaudRate(void) {
  //Serial.print("\nsetup Canbus baud rate\n");
  char ch;
  // select standard or extended CanID and read value
  if ((ch = command(" - Baudrate  enter 1 - 125kbs 2- 250kbs  3 - 500kbs ? ", "123", true)) <= ' ') return;
  switch (toupper(ch)) {  // baudrate change
    case '1':
      config = TWAI_TIMING_CONFIG_125KBITS();
      Serial.printf("\nBaudrate 125kbs set\n");
      break;
    case '2':
      config = TWAI_TIMING_CONFIG_250KBITS();
      Serial.printf("\nBaudrate 250kbs set\n");
      break;
    case '3':
      config = TWAI_TIMING_CONFIG_500KBITS();
      Serial.printf("\nBaudrate 500kbs set\n");
      break;
  }
  setupCanbus(true, config, extended, mask, filter);
  //Serial.println("Baudrate updated");
}

// transmit Can RTR or data frame
void serialToCanTx(const bool rtr) {
  // Configure message to transmit
  twai_message_t txFrame;
  //Serial.print("\nTransmit Canbus frame\n");
  char ch;
  // select standard or extended CanID and read value
  if ((ch = command(" - Transmit enter S - Standard or E - Extended CanID ? ", "SE", true)) <= ' ') return;
  uint32_t canID;
  switch (toupper(ch)) {
    case 'S':
      txFrame.extd = 0;
      txFrame.identifier = readCanID("\nenter standard 11-bit CanID (0 to 0x7FF) ? ", 0x7FF);
      break;
    case 'E':
      txFrame.extd = 1;
      txFrame.identifier = readCanID("\nenter Extended 29-bit CanID (0 to 0x1FFFFFFF) ? ", 0x1FFFFFFF);
      break;
  }
  char text[50] = { 0 };
  uint8_t data[8] = { 0 };
  txFrame.data_length_code = 0;
  if (rtr) txFrame.rtr = rtr;  // RTR frame
  else {
    Serial.print("Enter data up to 8 hex bytes ? ");  // data frame
    text[0] = 0;
    while (Serial.readBytesUntil('\n', text, 50) <= 0) delay(10);
    Serial.printf("Serial data avalilabe %s\n", text);
    // attempt to read  up to 8 data values
    int data_len = sscanf(text, "%2x%2x%2x%2x%2x%2x%2x%2x",
                          &data[0], &data[1], &data[2], &data[3], &data[4], &data[5], &data[6], &data[7]);
    //Serial.printf("data_len %d\n", data_len);
    txFrame.data_length_code = data_len;  // Actual data length
    for (int i = 0; i < data_len; i++)
      txFrame.data[i] = data[i];
  }
  int result = 0;
  // transmit the frame
  if ((result = twai_transmit(&txFrame, pdMS_TO_TICKS(1000))) == ESP_OK) {
    Serial.print("Message queued for transmission ");
    Serial.println();
    Serial.print("Sent CAN ID 0x");
    Serial.print(txFrame.identifier, HEX);
    if (rtr) Serial.print(" RTR");  // display RTR or data
    else {
      Serial.print("  Data: ");
      for (int i = 0; i < txFrame.data_length_code; i++) {
        if (data[i] < 0x10) Serial.print('0');
        Serial.print(data[i], HEX);
        Serial.print(' ');
      }
    }
    Serial.println();
  } else {
    Serial.print("!! Transmission failed!! Error code: ");
    Serial.println(result);
  }

}  // end serialToCanTx()


// receive message
static void handle_rx_message(twai_message_t &message) {
  static byte test = 0;
  static int messageNO = 0, errors = 0;
  messageNO++;
  // Process received message
  if (message.extd)
    Serial.printf("Rx %d EXT ID %lx data ", messageNO, message.identifier);
  else
    Serial.printf("Rx %d STD ID %lx data ", messageNO, message.identifier);
  if (message.rtr)
    Serial.println("RTR frame");
  else {
    for (int i = 0; i < message.data_length_code; i++) {
      Serial.printf("%02x", message.data[i]);
    }
#ifdef CHECK_SEQ_NUMBERS
    if (message.data[0] != test) {
      errors++;
      Serial.printf("\nERROR! expaected %d received %d ", test, message.data[0]);
      test = message.data[0];
    }
    test++;
    Serial.printf(" errors %d", errors);
#endif
    Serial.println();
  }
}
