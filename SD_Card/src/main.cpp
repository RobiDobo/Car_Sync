#include <SPI.h>
#include <SD.h>

#define SD_CS 10   // or another free GPIO if you prefer

void setup() {
  Serial.begin(115200);

  // Use default SPI pins (MOSI=11, MISO=13, SCLK=12)
  if (!SD.begin(SD_CS)) {
    Serial.println("Card Mount Failed");
  } else {
    Serial.println("Card Mounted");
  }
}
