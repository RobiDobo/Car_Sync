#ifndef ARDUINO_USB_MODE
#error This ESP32 SoC has no Native USB interface
#elif ARDUINO_USB_MODE == 1
#warning This sketch should be used when USB is in OTG mode
#endif

#include <Arduino.h>
#include <SPI.h>
// #include <SD.h> // REMOVED: Conflict with SdFat
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <USB.h>
#include <USBMSC.h>
#include "SdFat.h" 

#include <vector>
#include "Arduino_GFX_Library.h"

#include "miniz.h"

// --- CONFIGURATION ---
#define SCROLL_BUTTON_PIN 14
#define BOOT_BUTTON_PIN 0 
#define SD_CS 10

#ifndef WIFI_SSID
#define WIFI_SSID "MyNetwork"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "1976@bond"
#endif

// --- DISPLAY SETUP (T-Display S3) ---
#define GFX_EXTRA_PRE_INIT() \
  { \
    pinMode(15 /* PWD */, OUTPUT); \
    digitalWrite(15 /* PWD */, HIGH); \
  }
#define GFX_BL 38

Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(
  7 /* DC */, 6 /* CS */, 8 /* WR */, 9 /* RD */,
  39 /* D0 */, 40 /* D1 */, 41 /* D2 */, 42 /* D3 */, 45 /* D4 */, 46 /* D5 */, 47 /* D6 */, 48 /* D7 */);
Arduino_GFX *gfx = new Arduino_ST7789(bus, 5 /* RST */, 0 /* rotation */, true /* IPS */, 170 /* width */, 320 /* height */, 35 /* col offset 1 */, 0 /* row offset 1 */, 35 /* col offset 2 */, 0 /* row offset 2 */);

// --- GLOBALS ---
SdFat sd; // THE SINGLE SOURCE OF TRUTH FOR SD CARD
USBMSC MSC;
// Using Arduino-ESP32 core USB MSC (global objects `USB` and `MSC`)


static std::vector<String> g_fileLines;
static std::vector<String> g_filePaths;
static std::vector<bool> g_fileIsDir;
static int g_firstLine = 0;
static int g_linesPerPage = 0;
static int g_lineHeight = 10;
static int g_selectedIndex = 0;
static String g_currentPath = "/";

static const int buttonPin = SCROLL_BUTTON_PIN;
static int lastButtonState = HIGH;
static unsigned long lastDebounceTime = 0;
static const unsigned long debounceDelay = 50;

const uint32_t BLOCK_SIZE = 512;

// Disable files that are NOT under playlistPrefix by renaming them with ".nomsc"
// Returns number of files renamed
static size_t disableNonPlaylistFiles(const String &playlistPrefix) {
  if (!sd.card()) return 0;
  size_t disabled = 0;
  int total=0;
  std::function<void(const String&)> walk = [&](const String &path) {
    File32 dir = sd.open(path.c_str());
    if (!dir) return;
    dir.rewindDirectory();
    File32 entry = dir.openNextFile();
    char nameBuf[256];
    while (entry) {
      entry.getName(nameBuf, sizeof(nameBuf));
      String name = String(nameBuf);
      String full = path;
      if (!full.endsWith("/")) full += "/";
      full += name;

      if (entry.isDirectory()) {
        entry.close();
        walk(full);
      } else {
        total++;
        String lower = name;
        lower.toLowerCase();
        bool isAudio = lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".m4a") || lower.endsWith(".flac");
        if (isAudio) {
          // if file is NOT part of the chosen playlist prefix, disable it
          if (!playlistPrefix.isEmpty() && !full.startsWith(playlistPrefix)) {
            String newFull = full + ".nomsc";
            if (sd.rename(full.c_str(), newFull.c_str())) {
              disabled++;
              Serial.printf("Disabled %s -> %s\n", full.c_str(), newFull.c_str());
            }
          }
        }
      }
      if (entry.isOpen()) entry.close();
      entry = dir.openNextFile();
      Serial.printf("Found audio: %d  keep=%u\n", total, (unsigned)(total-disabled));
    }
    dir.close();
  };
  
  walk(String("/"));
  return disabled;
}

//restore files ending with .nomsc by removing suffix
static void restoreNomscOnBoot() {
  if (!sd.card()) return;

  std::function<void(const String&)> walk = [&](const String &path) {
    File32 dir = sd.open(path.c_str());
    if (!dir) return;
    dir.rewindDirectory();
    File32 entry = dir.openNextFile();
    char nameBuf[256];
    while (entry) {
      entry.getName(nameBuf, sizeof(nameBuf));
      String name = String(nameBuf);
      String full = path;
      if (!full.endsWith("/")) full += "/";
      full += name;

      if (entry.isDirectory()) {
        entry.close();
        walk(full);
      } else {
        const String suffix = ".nomsc";
        if (name.endsWith(suffix)) {
          String origName = name.substring(0, name.length() - suffix.length());
          String origFull = path;
          if (!origFull.endsWith("/")) origFull += "/";
          origFull += origName;
          // If original name already exists, choose a fallback (append _restored)
          if (sd.exists(origFull.c_str())) {
            String backup = origFull + String("_restored");
            Serial.printf("Conflict restoring %s -> %s, using %s\n", full.c_str(), origFull.c_str(), backup.c_str());
            if (sd.rename(full.c_str(), backup.c_str())) {
              Serial.printf("Restored to %s\n", backup.c_str());
            }
          } else {
            if (sd.rename(full.c_str(), origFull.c_str())) {
              Serial.printf("Restored %s -> %s\n", full.c_str(), origFull.c_str());
            } else {
              Serial.printf("Failed to restore %s\n", full.c_str());
            }
          }
        }
      }

      if (entry.isOpen()) entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
  };

  walk(String("/"));

  // Ensure metadata is flushed
  if (sd.card()) sd.card()->syncBlocks();
}



// --- Core USBMSC callbacks (Arduino-ESP32 core) ---
// Note: signatures mirror the USBMSC example in Arduino-ESP32 core
static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  // Write `bufsize` bytes starting at sector `lba` + `offset` into the SD card.
  if (!sd.card()) return -1;
  uint32_t sectors = sd.card()->sectorCount();
  if (lba >= sectors) return -1;

  uint32_t remaining = bufsize;
  uint8_t *src = buffer;
  uint32_t sector = lba;
  uint32_t off = offset;

  uint8_t tmp[BLOCK_SIZE];

  while (remaining > 0) {
    // If we are aligned to sector boundary and have at least one full sector, write directly
    if (off == 0 && remaining >= BLOCK_SIZE) {
      uint32_t n = remaining / BLOCK_SIZE;
      // limit n so we don't go past card
      if (sector + n > sectors) return -1;
      if (!sd.card()->writeSectors(sector, src, n)) return -1;
      sector += n;
      src += n * BLOCK_SIZE;
      remaining -= n * BLOCK_SIZE;
      // off stays zero
      continue;
    }

    // Partial sector: read-modify-write
    if (!sd.card()->readSectors(sector, tmp, 1)) return -1;
    uint32_t toCopy = BLOCK_SIZE - off;
    if (toCopy > remaining) toCopy = remaining;
    memcpy(tmp + off, src, toCopy);
    if (!sd.card()->writeSectors(sector, tmp, 1)) return -1;

    remaining -= toCopy;
    src += toCopy;
    sector++;
    off = 0;
  }

  return (int32_t)bufsize;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  // Read `bufsize` bytes starting at sector `lba` + `offset` from the SD card.
  if (!sd.card()) return -1;
  uint32_t sectors = sd.card()->sectorCount();
  if (lba >= sectors) return -1;

  uint32_t remaining = bufsize;
  uint8_t *dst = (uint8_t*)buffer;
  uint32_t sector = lba;
  uint32_t off = offset;

  uint8_t tmp[BLOCK_SIZE];

  while (remaining > 0) {
    // If we are aligned and need whole sectors, read directly
    if (off == 0 && remaining >= BLOCK_SIZE) {
      uint32_t n = remaining / BLOCK_SIZE;
      if (sector + n > sectors) return -1;
      if (!sd.card()->readSectors(sector, dst, n)) return -1;
      sector += n;
      dst += n * BLOCK_SIZE;
      remaining -= n * BLOCK_SIZE;
      continue;
    }

    // Partial sector: read sector then copy
    if (!sd.card()->readSectors(sector, tmp, 1)) return -1;
    uint32_t toCopy = BLOCK_SIZE - off;
    if (toCopy > remaining) toCopy = remaining;
    memcpy(dst, tmp + off, toCopy);

    remaining -= toCopy;
    dst += toCopy;
    sector++;
    off = 0;
  }

  return (int32_t)bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  Serial.printf("MSC START/STOP: power: %u, start: %u, eject: %u\n", power_condition, start, load_eject);
  return true;
}

static void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == ARDUINO_USB_EVENTS) {
    arduino_usb_event_data_t *data = (arduino_usb_event_data_t *)event_data;
    switch (event_id) {
      case ARDUINO_USB_STARTED_EVENT: Serial.println("USB PLUGGED"); break;
      case ARDUINO_USB_STOPPED_EVENT: Serial.println("USB UNPLUGGED"); break;
      case ARDUINO_USB_SUSPEND_EVENT: Serial.printf("USB SUSPENDED: remote_wakeup_en: %u\n", data->suspend.remote_wakeup_en); break;
      case ARDUINO_USB_RESUME_EVENT:  Serial.println("USB RESUMED"); break;

      default: break;
    }
  }
}

// --- HELPERS ---
static String humanReadableSize(uint64_t bytes) {
  char buf[32];
  if (bytes < 1024) {
    snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
  } else if (bytes < 1024ULL * 1024ULL) {
    snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
  } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
    snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
  } else {
    snprintf(buf, sizeof(buf), "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
  }
  return String(buf);
}

static void drawPopUp(unsigned long remaining){
    int W = gfx->width();
    int H = gfx->height();
    int boxW = (W * 2) / 3;
    int boxH = 40;
    int boxX = (W - boxW) / 2;
    int boxY = (H - boxH) / 2;
    gfx->fillRoundRect(boxX, boxY, boxW, boxH, 6, RED);
    gfx->drawRoundRect(boxX, boxY, boxW, boxH, 6, WHITE);
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    char buf[32];
    snprintf(buf, sizeof(buf), "Hold %lu ms more", remaining);
    gfx->setCursor(boxX + 10, boxY + (boxH / 2) - 8);
    gfx->println(buf);
    
}

// --- FILE LISTING (SdFat Version) ---
void listFilesAndPrintSamples(const char *path = "/") {
  // SdFat uses 'File' (which is usually File32 or ExFile)
  File32 root = sd.open(path);
  if (!root) {
    Serial.printf("Failed to open dir: %s\n", path);
    return;
  }

  Serial.print("Opened path: ");
  Serial.println(path);
  // SdFat directory check
  if (!root.isDirectory()) {
     Serial.println("Not a directory");
     root.close();
     return;
  }
  root.rewindDirectory(); // Rewind directory

  g_fileLines.clear();
  g_filePaths.clear();
  g_fileIsDir.clear();
  // Reserve to reduce reallocations and heap churn during listing
  g_fileLines.reserve(128);
  g_filePaths.reserve(128);
  g_fileIsDir.reserve(128);

  File32 entry = root.openNextFile();
  char nameBuf[128] = {0}; // zero-init to avoid leftover garbage

  while (entry) {
    // Defensive: ensure the File32 is actually open/valid
    if (!entry.isOpen()) {
      entry.close();
      entry = root.openNextFile();
      continue;
    }

    // Use getName() into a local, zeroed buffer to avoid relying on
    // pointer lifetimes or unexpected return values from entry.name().
    memset(nameBuf, 0, sizeof(nameBuf));
    entry.getName(nameBuf, sizeof(nameBuf));
    String name = String(nameBuf);
    
    // Filter hidden files if needed (optional)
    // if (name.startsWith(".")) { entry.close(); continue; }

    String fullPath = String(path);
    if (!fullPath.endsWith("/")) fullPath += "/";
    fullPath += name;

    if (entry.isDirectory()) {
      // Serial.print("DIR:  "); Serial.println(name);
      g_fileLines.push_back(String("DIR: ") + name);
      g_fileIsDir.push_back(true);
      g_filePaths.push_back(fullPath);
    } else {
      uint64_t sz = entry.size();
      String sizeStr = humanReadableSize(sz);
      // Serial.print("FILE: "); Serial.println(name);
      g_fileLines.push_back(name + String("  ") + sizeStr);
      g_fileIsDir.push_back(false);
      g_filePaths.push_back(fullPath);
    }
    entry.close();
    // Give background tasks a chance to run (TCP, TinyUSB background work)
    yield();
    entry = root.openNextFile();
  }

  if (g_fileLines.empty()) {
    g_fileLines.push_back(String("(no files found)"));
  }

  root.close();

  // --- UI DRAWING (Same as before) ---
  const int textSize = 1;
  gfx->setTextSize(textSize);
  g_lineHeight = 12 * textSize + 6;
  int startY = 24;
  int screenH = gfx->height();
  g_linesPerPage = (screenH - startY - 8) / g_lineHeight;
  if (g_linesPerPage < 1) g_linesPerPage = 1;
  g_firstLine = 0;
  g_currentPath = String(path);
  g_selectedIndex = 0;

  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setCursor(10, 6);
  gfx->println("Files:");

  int y = startY;
  int end = (int)g_fileLines.size();
  if (end > g_firstLine + g_linesPerPage) end = g_firstLine + g_linesPerPage;
  for (int i = g_firstLine; i < end; ++i) {
    if (i == g_selectedIndex) {
      gfx->fillRect(4, y - 2, gfx->width() - 8, g_lineHeight, WHITE);
      gfx->setTextColor(BLACK);
    } else {
      gfx->setTextColor(WHITE);
    }
    gfx->setCursor(6, y);
    gfx->print(g_fileLines[i]);
    y += g_lineHeight;
  }
  gfx->setTextColor(WHITE);
  gfx->setCursor(6, gfx->height() - 12);
  int page = g_firstLine / g_linesPerPage + 1;
  int pages = ((int)g_fileLines.size() + g_linesPerPage - 1) / g_linesPerPage;
  char buf[48];
  snprintf(buf, sizeof(buf), "Pg %d/%d (IO14=Sel, BOOT=Back/Enter)", page, pages);
  gfx->println(buf);
}

// Draw current page from `g_fileLines` using `g_selectedIndex` and paging.
static void drawCurrentPage() {
  // Safety: ensure layout metrics are available
  // Use the same text size/layout as the main listing
  const int textSize = 1;
  gfx->setTextSize(textSize);
  g_lineHeight = 12 * textSize + 6;
  if (g_linesPerPage <= 0) {
    int startY = 24;
    int screenH = gfx->height();
    g_linesPerPage = (screenH - startY - 8) / g_lineHeight;
    if (g_linesPerPage < 1) g_linesPerPage = 1;
  }

  // Normalize selection
  int total = (int)g_fileLines.size();
  if (total == 0) {
    gfx->fillScreen(BLACK);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10,6);
    gfx->println("Files:");
    gfx->setCursor(6, gfx->height() - 12);
    gfx->println("(no files)");
    return;
  }
  if (g_selectedIndex < 0) g_selectedIndex = 0;
  if (g_selectedIndex >= total) g_selectedIndex = total - 1;

  // Page the selection into view
  g_firstLine = (g_selectedIndex / g_linesPerPage) * g_linesPerPage;
  if (g_firstLine < 0) g_firstLine = 0;

  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setCursor(10,6); gfx->println("Files:");

  int y = 24;
  int end = g_firstLine + g_linesPerPage;
  if (end > total) end = total;
  for (int i = g_firstLine; i < end; ++i) {
    if (i == g_selectedIndex) {
      gfx->fillRect(4, y - 2, gfx->width() - 8, g_lineHeight, WHITE);
      gfx->setTextColor(BLACK);
    } else {
      gfx->setTextColor(WHITE);
    }
    gfx->setCursor(6, y);
    gfx->print(g_fileLines[i]);
    y += g_lineHeight;
  }
  gfx->setTextColor(WHITE);
  gfx->setCursor(6, gfx->height() - 12);
  int page = g_firstLine / g_linesPerPage + 1;
  int pages = ((int)g_fileLines.size() + g_linesPerPage - 1) / g_linesPerPage;
  char buf[48];
  snprintf(buf, sizeof(buf), "Pg %d/%d (IO14=Sel, BOOT=Back/Enter)", page, pages);
  gfx->println(buf);
}

// --- LOGICAL PATH LISTING (SdFat Version) ---
void listFilesForLogicalPath(const String &logicalPrefix) {
  g_fileLines.clear();
  g_filePaths.clear();
  g_fileIsDir.clear();

  // Recursive walker for SdFat
  // Note: Recursive lambdas with capture are tricky in C++, using std::function
  std::function<void(const String&)> walk = [&](const String &path) {
    File32 dir = sd.open(path.c_str());
    if (!dir) return;
    if (!dir.isDirectory()) { dir.close(); return; }
    
    dir.rewindDirectory();
    File32 entry = dir.openNextFile();
    char nameBuf[256];

    while (entry) {
      entry.getName(nameBuf, sizeof(nameBuf));
      String name = String(nameBuf);
      String full = path;
      if (!full.endsWith("/")) full += "/";
      full += name;

      if (entry.isDirectory()) {
        // Recurse
        // Close entry before recursing to save file handles if deep
        entry.close(); 
        walk(full);
      } else {
        // Check match
        if (full.startsWith(logicalPrefix)) {
           String rel = full.substring(logicalPrefix.length());
           if (rel.startsWith("/")) rel = rel.substring(1);
           String sizeStr = humanReadableSize(entry.size());
           g_fileLines.push_back(rel + String("  ") + sizeStr);
           g_filePaths.push_back(full);
           g_fileIsDir.push_back(false);
        }
        if (entry.isOpen()) entry.close();
        entry = dir.openNextFile();
      }
    }
    dir.close();
  };

  walk("/"); // Start from root

  if (g_fileLines.empty()) g_fileLines.push_back(String("(no files found)"));

  // --- UI DRAWING (Simplified reuse) ---
  g_firstLine = 0;
  g_currentPath = logicalPrefix;
  g_selectedIndex = 0;
  
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setCursor(10, 6);
  gfx->println(String("F:") + logicalPrefix);

  // Draw only first page logic here for brevity (same as above)
  int y = 24;
  int end = (int)g_fileLines.size();
  if (end > g_linesPerPage) end = g_linesPerPage;
  
  for (int i = 0; i < end; ++i) {
     if (i == 0) { gfx->fillRect(4, y-2, gfx->width()-8, g_lineHeight, WHITE); gfx->setTextColor(BLACK); }
     else gfx->setTextColor(WHITE);
     gfx->setCursor(6, y);
     gfx->print(g_fileLines[i]);
     y += g_lineHeight;
  }
  gfx->setTextColor(WHITE);
}

// --- TRASH HELPERS (SdFat Version) ---
static bool ensureTrashDir() {
  if (sd.exists("/.trash")) return true;
  return sd.mkdir("/.trash");
}

static String makeTrashPath(const char *origPath) {
  String name = String(origPath);
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  static unsigned long seq = 0;
  seq++;
  char buf[96];
  snprintf(buf, sizeof(buf), "/.trash/%lu_%lu_%s", (unsigned long)millis(), (unsigned long)seq, name.c_str());
  return String(buf);
}

static bool moveToTrash(const char *path) {
  if (!ensureTrashDir()) return false;
  String dest = makeTrashPath(path);
  
  if (sd.rename(path, dest.c_str())) {
    Serial.printf("Renamed %s -> %s\n", path, dest.c_str());
    return true;
  }
  
  // Directory move fallback (SdFat rename usually works for dirs on same volume)
  // For brevity, relying on rename. If rename fails across dirs, deep copy needed.
  return false; 
}

static size_t moveAllToTrash(const char *path = "/") {
  File32 root = sd.open(path);
  if (!root) return 0;
  
  size_t moved = 0;
  File32 entry = root.openNextFile();
  char nameBuf[256];
  
  // Note: modifying directory while iterating can be tricky.
  // Safer to collect list then move.
  std::vector<String> toMove;
  
  while (entry) {
    entry.getName(nameBuf, sizeof(nameBuf));
    String name = String(nameBuf);
    if (name != "System Volume Information" && name != ".trash") {
        String full = String(path);
        if (!full.endsWith("/")) full += "/";
        full += name;
        toMove.push_back(full);
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();

  for (const auto &f : toMove) {
      if (moveToTrash(f.c_str())) moved++;
  }
  return moved;
}

//helper for download progress

// Draw download progress on the screen. Shows filename, MB downloaded,
// percent if content length known, and a simple progress bar.
static void drawDownloadProgress(size_t bytesWritten, int contentLength, unsigned long elapsedMs, const String &label) {
  const int W = gfx->width();
  const int H = gfx->height();
  const int boxW = W - 20;
  const int boxH = 44;
  const int boxX = 10;
  const int boxY = H - boxH - 10;

  gfx->fillRoundRect(boxX, boxY, boxW, boxH, 6, BLACK);
  gfx->drawRoundRect(boxX, boxY, boxW, boxH, 6, WHITE);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(1);

  // Shorten label if necessary
  String name = label;
  if (name.length() > 22) name = name.substring(name.length() - 22);

  gfx->setCursor(boxX + 6, boxY + 6);
  gfx->print(name);

  double mb = (double)bytesWritten / (1024.0 * 1024.0);
  char buf[64];
  if (contentLength > 0) {
    double pct = (double)bytesWritten * 100.0 / (double)contentLength;
    snprintf(buf, sizeof(buf), "%.2f MB (%.0f%%)", mb, pct);
  } else {
    snprintf(buf, sizeof(buf), "%.2f MB", mb);
  }
  gfx->setCursor(boxX + 6, boxY + 18);
  gfx->print(buf);

  // Progress bar
  int barX = boxX + 6;
  int barY = boxY + 30;
  int barW = boxW - 12;
  int barH = 6;
  gfx->drawRect(barX - 1, barY - 1, barW + 2, barH + 2, WHITE);
  int fillW = 0;
  if (contentLength > 0) {
    fillW = (int)(((long long)bytesWritten * (long long)barW) / (long long)contentLength);
    if (fillW > barW) fillW = barW;
  } else {
    // Indeterminate: simple animated dot based on elapsedMs
    int anim = (elapsedMs / 200) % (barW);
    fillW = anim;
  }
  if (fillW > 0) gfx->fillRect(barX, barY, fillW, barH, GREEN);
}

// Draw SD speed test results (write/read MB/s)
static void drawSdSpeedResult(float writeMBps, float readMBps) {
  const int W = gfx->width();
  const int H = gfx->height();
  const int boxW = W - 20;
  const int boxH = 44;
  const int boxX = 10;
  const int boxY = H - boxH - 10;

  gfx->fillRoundRect(boxX, boxY, boxW, boxH, 6, BLACK);
  gfx->drawRoundRect(boxX, boxY, boxW, boxH, 6, WHITE);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(boxX + 6, boxY + 6);
  char buf[64];
  snprintf(buf, sizeof(buf), "SD Write: %.2f MB/s", writeMBps);
  gfx->print(buf);
  gfx->setCursor(boxX + 6, boxY + 20);
  snprintf(buf, sizeof(buf), "SD Read:  %.2f MB/s", readMBps);
  gfx->print(buf);
}

// Quick SD speed test: write and then read back a temporary file.
// Keeps the test size small by default to avoid long blocking time.
static void testSdSpeed(size_t testMB = 2) {
  if (!sd.card()) return;
  const String tmpPath = String("/.sd_speed_test.tmp");
  const size_t totalBytes = testMB * 1024UL * 1024UL;
  const size_t bufSize = 32768; // 32KB buffer
  uint8_t *buf = (uint8_t*)malloc(bufSize);
  if (!buf) return;
  // Fill buffer with pattern
  for (size_t i = 0; i < bufSize; ++i) buf[i] = (uint8_t)(i & 0xFF);

  // Write test
  File32 f = sd.open(tmpPath.c_str(), O_CREAT | O_WRITE | O_TRUNC);
  if (!f) { free(buf); return; }
  size_t written = 0;
  unsigned long t0 = millis();
  while (written < totalBytes) {
    size_t toWrite = bufSize;
    if (written + toWrite > totalBytes) toWrite = totalBytes - written;
    size_t w = f.write(buf, toWrite);
    if (w != toWrite) break;
    written += w;
    yield();
  }
  f.close();
  unsigned long writeMs = millis() - t0;

  // Read test
  File32 fr = sd.open(tmpPath.c_str(), O_READ);
  size_t readBytes = 0;
  unsigned long t1 = millis();
  if (fr) {
    while (readBytes < written) {
      int r = fr.read(buf, (readBytes + bufSize > written) ? (written - readBytes) : bufSize);
      if (r <= 0) break;
      readBytes += r;
      yield();
    }
    fr.close();
  }
  unsigned long readMs = millis() - t1;

  // Cleanup
  sd.remove(tmpPath.c_str());
  free(buf);

  double writeSec = (writeMs > 0) ? (writeMs / 1000.0) : 0.0;
  double readSec = (readMs > 0) ? (readMs / 1000.0) : 0.0;
  double writeMB = (double)written / (1024.0 * 1024.0);
  double readMB = (double)readBytes / (1024.0 * 1024.0);
  float writeMBps = (writeSec > 0.0) ? (float)(writeMB / writeSec) : 0.0f;
  float readMBps = (readSec > 0.0) ? (float)(readMB / readSec) : 0.0f;

  Serial.printf("SD speed test: wrote %.2f MB in %lu ms (%.2f MB/s), read %.2f MB in %lu ms (%.2f MB/s)\n",
                writeMB, (unsigned long)writeMs, writeMBps, readMB, (unsigned long)readMs, readMBps);

  // Show results on-screen
  drawSdSpeedResult(writeMBps, readMBps);
  delay(1200); // leave result visible briefly
}

// --- DOWNLOADER (SdFat Version) ---
static bool downloadToSD(const String &url, const String &sdPath) {
    WiFiClientSecure client;
    client.setInsecure(); 
    client.setTimeout(10000); 

    HTTPClient http;
    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    String tmp = sdPath + ".tmp";

    // Ensure dir (SdFat mkdir is robust)
    int p = sdPath.lastIndexOf('/');
    if (p > 0) {
        String parent = sdPath.substring(0, p);
        if (!sd.exists(parent.c_str())) sd.mkdir(parent.c_str(), true); // true = p_flag (recursive)
    }

    // Open with O_CREAT | O_WRITE | O_TRUNC
    File32 f = sd.open(tmp.c_str(), O_CREAT | O_WRITE | O_TRUNC);
    if (!f) {
        http.end();
        return false;
    }

    uint8_t *buf = (uint8_t*)malloc(32768);
    if (!buf) { f.close(); http.end(); return false; }

    size_t bytesWritten = 0;
    unsigned long lastByteTime = millis();
    //timing
    unsigned long t0 = millis(); // start timing
    unsigned long lastUpdate = 0;

    while (http.connected() || stream->available()) {
        size_t size = stream->available();
        if (size > 0) {
            size_t readSize = (size > 32768) ? 32768 : size;
            int c = stream->readBytes(buf, readSize);
            if (c > 0) {
                f.write(buf, c);
                bytesWritten += c;
                lastByteTime = millis();
                yield(); 
            }
        } else {
            if (millis() - lastByteTime > 5000) break;
            delay(1);
        }

      // Update on-screen progress every 200ms
      unsigned long now = millis();
      if (now - lastUpdate >= 200) {
        // Try to get content length (may be 0 if unknown)
        int contentLength = http.getSize();
        drawDownloadProgress(bytesWritten, contentLength, now - t0, sdPath);
        lastUpdate = now;
      }
    }
    unsigned long elapsedMs = millis() - t0; // end timing
    free(buf);
    f.close();
    http.end();

    //if (sd.exists(sdPath.c_str())) sd.remove(sdPath.c_str());
    //return sd.rename(tmp.c_str(), sdPath.c_str());
    bool ok = true;
    if (sd.exists(sdPath.c_str())) sd.remove(sdPath.c_str());
    if (!sd.rename(tmp.c_str(), sdPath.c_str())) ok = false;

    // Print MB/s stats (best-effort). Avoid divide-by-zero.
    double seconds = (elapsedMs > 0) ? (elapsedMs / 1000.0) : 0.0;
    double mb = (double)bytesWritten / (1024.0 * 1024.0);
    double mbps = (seconds > 0.0) ? (mb / seconds) : 0.0;
    Serial.printf("Download %s -> %s : %lu bytes in %lu ms (%.2f MB, %.2f MB/s) %s\n",
                  url.c_str(), sdPath.c_str(), (unsigned long)bytesWritten, (unsigned long)elapsedMs, mb, mbps,
                  ok ? "OK" : "FAILED");

    // Final on-screen update
    int contentLengthFinal = http.getSize();
    drawDownloadProgress(bytesWritten, contentLengthFinal, elapsedMs, sdPath);

    // Show completion/failure message briefly
    gfx->setTextSize(1);
    gfx->setCursor(6, gfx->height() - 26);
    if (ok) {
      gfx->setTextColor(GREEN);
      gfx->println("Download complete");
    } else {
      gfx->setTextColor(RED);
      gfx->println("Download failed");
    }

    return ok;
}


// URL-encode filename for safe URL building
static String urlEncode(const String &s) {
  String enc;
  enc.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '/') {
      enc += '/';
    } else if ( (c >= '0' && c <= '9') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
               c == '-' || c == '_' || c == '.' || c == '~' ) {
      enc += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      enc += buf;
    }
  }
  return enc;
}

//zip stuff

// Ensure parent directories exist for a given path using SdFat (recursive mkdir)
static void ensureParentDirs(const String &path) {
  int p = path.lastIndexOf('/');
  if (p <= 0) return;
  String dir = path.substring(0, p);
  // Create hierarchy; sd.mkdir(..., true) creates recursively in SdFat fork used above
  if (!sd.exists(dir.c_str())) {
    sd.mkdir(dir.c_str(), true);
    Serial.printf("mkdir %s\n", dir.c_str());
  }
}

// Callback wrapper to let miniz read from an Arduino File object
size_t miniz_file_read_func(void *pOpaque, mz_uint64 file_ofs, void *pBuf, size_t n) {
    File32 *pFile = (File32 *)pOpaque; // Cast the opaque pointer back to a File object

    // Ensure we are at the correct location
    if (pFile->position() != file_ofs) {
        if (!pFile->seek(file_ofs)) {
            return 0; // Seek failed
        }
    }

    // Read the data
    return pFile->read(pBuf, n);
}
// --- WRITER CALLBACK: Writes extracted chunks to the destination file ---
size_t miniz_file_write_func(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n) {
    File32 *pFile = (File32 *)pOpaque;
    
    // Write the chunk to SD
    // Note: We assume sequential writing, so we rarely need to seek. 
    // However, for safety regarding file_ofs, we usually just trust the stream.
    return pFile->write((const uint8_t*)pBuf, n);
}

// Unzip zipPath (SD path) into destRoot (SD path, e.g. "/")
// Returns true on success (best-effort; some file extracts may fail but function returns)
static bool unzipZipToSD(const char *zipPath, const char *destRoot) {
    Serial.printf("Unzip Streaming: %s -> %s\n", zipPath, destRoot);

    // 1. Open Source Zip
    File32 zipFile = sd.open(zipPath, FILE_READ);
    if (!zipFile) {
        Serial.println("FAILED: Could not open zip file.");
        return false;
    }

    // 2. Init Miniz with Custom Reader
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = miniz_file_read_func;   // Assign Reader Callback
    zip.m_pIO_opaque = &zipFile;          // Pass the zip File object

    if (!mz_zip_reader_init(&zip, zipFile.size(), 0)) {
        Serial.println("mz_zip_reader_init FAILED.");
        zipFile.close();
        return false;
    }

    int fileCount = (int)mz_zip_reader_get_num_files(&zip);
    Serial.printf("Files found: %d\n", fileCount);

    char destPath[256]; 
    mz_zip_archive_file_stat file_stat;

    // 3. Iterate and Stream
    for (int i = 0; i < fileCount; i++) {
        if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) continue;

        // Build Destination Path
        snprintf(destPath, sizeof(destPath), "%s%s%s", 
                 destRoot, 
                 (destRoot[strlen(destRoot)-1] == '/') ? "" : "/", 
                 file_stat.m_filename);

        // Check if it is a directory entry
        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            sd.mkdir(destPath);
            Serial.printf("DIR: %s\n", destPath);
            continue;
        }

        // 4. Handle File Extraction (Streaming)
        
        // Create the destination file on SD
        File32 destFile = sd.open(destPath, FILE_WRITE);
        if (!destFile) {
            Serial.printf("ERR: Create failed %s\n", destPath);
            continue;
        }

        // *** THE MAGIC PART ***
        // Extract using callback. We pass &destFile as the opaque pointer.
        // miniz will call miniz_file_write_func repeatedly with chunks of data.
        if (!mz_zip_reader_extract_to_callback(&zip, i, miniz_file_write_func, &destFile, 0)) {
            Serial.printf("FAILED extraction: %s\n", destPath);
            destFile.close();
            // Optional: delete partial file
            sd.remove(destPath);
        } else {
            Serial.printf("OK: %s\n", destPath);
            destFile.close();
        }
    }

    mz_zip_reader_end(&zip);
    zipFile.close();
    return true;
}

// --- SYNC (SdFat Version) ---
bool syncFromWorkerOnly(const char *workerBaseUrl) {

  String base = String(workerBaseUrl);
  if (!base.endsWith("/")) base += "/";

  // manifest is at the worker root
  String listUrl = base; // e.g. "https://music-worker.../"

  WiFiClientSecure client;
  client.setInsecure(); // replace with setCACert(...) for production
  HTTPClient http;
  if (!http.begin(client, listUrl)) {
    Serial.println("Failed to begin list URL");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("List GET failed: %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  const size_t JSON_DOC_CAPACITY = 12 * 1024;
  DynamicJsonDocument doc(JSON_DOC_CAPACITY);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return false;
  }
  if (!doc.is<JsonArray>()) {
    Serial.println("List not an array");
    return false;
  }

  std::vector<String> wanted;
  for (JsonVariant v : doc.as<JsonArray>()) {
    if (!v.is<const char*>()) continue;
    String name = String((const char*)v.as<const char*>());
    if (name.length() == 0) continue;
    String sdPath = "/" + name;
    wanted.push_back(sdPath);

    // only download zip files — skip other entries
    if (!sdPath.endsWith(".zip")) {
      Serial.printf("Skipping non-zip listed item: %s\n", sdPath.c_str());
      continue;
    }

    // skip if exists
    File32 f = sd.open(sdPath.c_str());
    bool needDownload = !f;
    if (f) f.close();
    if (!needDownload) continue;

    String encoded = urlEncode(name);
    String fullUrl = base + encoded; // worker serves file at base/<encoded name>

    Serial.printf("Downloading: %s -> %s\n", fullUrl.c_str(), sdPath.c_str());
    if (!downloadToSD(fullUrl, sdPath)) {
      Serial.printf("Download failed for %s\n", fullUrl.c_str());
      // optional retry logic
    } else {
          Serial.printf("Saved %s\n", sdPath.c_str());
          // If it's a zip in root, extract into SD root
          if (sdPath.endsWith(".zip")) {
            Serial.println("Extracting zip...");
            if (unzipZipToSD(sdPath.c_str(), "/")) {
              Serial.println("Unzip OK");
              // optionally remove the zip to save space:
              sd.remove(sdPath.c_str());
            } else {
              Serial.println("Unzip FAILED");
            }
      }
    }
  }

  // move local files not in wanted list to .trash
  std::function<void(const String&)> walk = [&](const String &path) {
    File32 dir = sd.open(path.c_str());
    if (!dir) return;
    File32 entry = dir.openNextFile();
    while (entry) {
      String name = String(entry.name());
      String full = path;
      if (!full.endsWith("/")) full += "/";
      full += name;
      if (entry.isDirectory()) {
        walk(full);
      } else {
        bool keep = false;
        for (auto &w : wanted) if (w == full) { keep = true; break; }
        if (!keep) {
          Serial.printf("Moving to trash: %s\n", full.c_str());
          moveToTrash(full.c_str());
        }
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
  };
  walk(String("/"));

  listFilesAndPrintSamples("/");
  return true;
}


static bool connectWiFi(unsigned long timeoutMs = 15000) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) { delay(100); }
  return WiFi.status() == WL_CONNECTED;
}

bool init_usb(){
    // NO sd.begin() here! We do it once in setup.
    // Use Arduino-ESP32 core USB MSC API (MSC, USB globals)
    if (!sd.card() || sd.card()->errorCode()) {
      Serial.println("init_usb: no sd card or sd error");
      return false;
    }

    uint32_t blockCount = sd.card()->sectorCount();

    // Configure MSC metadata and callbacks (matches USBMSC example)
    MSC.vendorID("ESP32");
    MSC.productID("USB_MSC");
    MSC.productRevision("1.0");

    MSC.onStartStop(onStartStop);
    MSC.onRead(onRead);
    MSC.onWrite(onWrite);

    MSC.mediaPresent(true);
    // Some USBMSC implementations don't provide isWritable(); it's optional.
    // If your USBMSC class supports it, enable write support here.
    // MSC.isWritable(true);

    // Start MSC with size from SD
    MSC.begin(blockCount, BLOCK_SIZE);

    // Hook USB event and start USB core
    USB.onEvent(usbEventCallback);
    USB.begin();

    Serial.println("init_usb: MSC + USB started");
    return true;
}


void setup() {
  Serial.begin(115200);

  GFX_EXTRA_PRE_INIT();
  #ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  #endif
  gfx->begin();
  gfx->setRotation(3);
  gfx->fillScreen(BLACK);

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // 1. ONE SD INIT TO RULE THEM ALL
  // Try standard init
  gfx->setTextSize(2);
  if (!sd.begin(SD_CS, SD_SCK_MHZ(25))) { // 25MHz is safe
    Serial.println("SD Init Failed!");
    
    gfx->setTextColor(RED); gfx->println("SD Fail");
    Serial.println("SdFat sd.begin failed");
    Serial.printf("sd.sdErrorCode()=0x%02X sd.sdErrorData()=0x%08X\n",
                  sd.sdErrorCode(), sd.sdErrorData());

  } else {
    Serial.println("SD Mounted (SdFat)");
    // Restore any leftover .nomsc files from previous unexpected power-offs
    restoreNomscOnBoot();
    gfx->setTextColor(GREEN); gfx->println("SD OK");
    // Quick SD speed test to give immediate feedback on card performance
    gfx->setTextColor(WHITE);
    gfx->println("SD speed test...");
    testSdSpeed(2); // default 2 MB test
  }

  if(connectWiFi()) {
     gfx->setTextColor(GREEN); gfx->println("WiFi OK");
  }
  else {
     gfx->setTextColor(RED); gfx->println("WiFi Fail");
  }

  syncFromWorkerOnly("https://music-worker.robidobosan.workers.dev/"); // Call your sync here if needed
  delay(3000); // Wait a bit to show status
  gfx->setTextSize(1);
  listFilesAndPrintSamples("/");
  //init_usb(); // starts core MSC + USB, assumes sd is ready
}

void loop() {
  int reading = digitalRead(buttonPin);
  int boot_button = digitalRead(BOOT_BUTTON_PIN);
  if (reading != lastButtonState) lastDebounceTime = millis();

  static int stableState = HIGH;
  static unsigned long pressStart = 0;
  static bool longHandled = false;
  const unsigned long holdMs = 5000;
  const unsigned long popupDelay = 1000; // show popup after 1s hold
  static unsigned long lastPrint = 0;
  const unsigned long printInterval = 200;

  static int lastBootState = HIGH;
  static unsigned long lastBootDebounce = 0;
  static int stableBootState = HIGH;
  static unsigned long bootPressStart = 0;
  static bool bootLongHandled = false;

  // --- SCROLL BUTTON (IO14) ---
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != stableState) {
      if (reading == LOW) { // Press
        pressStart = millis();
        longHandled = false;
      } else { // Release
        unsigned long dur = (pressStart == 0) ? 0 : millis() - pressStart;
        if (!longHandled && dur < 1000) {
           // NEXT PAGE / ITEM: advance selection and redraw only
           int total = (int)g_fileLines.size();
           if (total > 0) {
             g_selectedIndex++;
             if (g_selectedIndex >= total) g_selectedIndex = 0;
             // Keep current listing and just redraw the page with new selection
             drawCurrentPage();
           }
        }
        pressStart = 0;
        drawCurrentPage();
      }
      stableState = reading;
    }
  }

    //IO14 check for long press
  if (stableState == LOW && pressStart != 0 && !longHandled) {
    unsigned long elapsed = millis() - pressStart;
    if (elapsed >= holdMs) {
      longHandled = true;
      gfx->fillScreen(BLACK);
      gfx->setTextColor(YELLOW);
      gfx->setCursor(6, 6);
      //gfx->println("Moving all to /.trash...");
      //Serial.println("User requested moveAllToTrash (long-press)");
      gfx->println("Preparing playlist...");
      Serial.println("User requested playlist MSC (long-press)");
      delay(1000);
      //size_t moved = moveAllToTrash("/");
      //Serial.printf("Moved %u entries to /.trash\n", (unsigned)moved);
      //listFilesAndPrintSamples("/");
      // Determine playlist prefix from current selection (use exact file path for single-file playlist)
      String playlistPrefix;
      if (g_selectedIndex >= 0 && g_selectedIndex < (int)g_filePaths.size()) {
        playlistPrefix = g_filePaths[g_selectedIndex];
        // If the selected entry is a file, keep that exact file only.
        // If it's a directory, keep everything under that directory.
        if (g_fileIsDir[g_selectedIndex]) {
          // ensure directory path ends with '/'
          if (!playlistPrefix.endsWith("/")) playlistPrefix += "/";
        } else {
          // exact file path; no change
        }
      } else {
        // fallback to current path
        playlistPrefix = g_currentPath;
        if (!playlistPrefix.endsWith("/")) playlistPrefix += "/";
      }

      Serial.printf("Playlist prefix: %s\n", playlistPrefix.c_str());
      Serial.println("cleaning up leftover files...");
      gfx->println("cleaning up leftover files...");
      restoreNomscOnBoot();//clean up any leftovers first
      size_t disabled = disableNonPlaylistFiles(playlistPrefix);
      String msg = "Disabled " + String((unsigned)disabled) + " files";
      gfx->println(msg);
      Serial.printf("Disabled %u files\n", (unsigned)disabled);

      // Redraw listing of current path so user sees changes
      listFilesAndPrintSamples(g_currentPath.c_str());
      init_usb(); // start usb after preparing playlist
    } else {
      unsigned long now = millis();
      // Only start showing popup after popupDelay has elapsed
      if (elapsed >= popupDelay) {
        if (lastPrint == 0) lastPrint = now; // first update anchor when popup starts
        // if enough time passed, advance the anchor by whole intervals (catch-up)
        if (now - lastPrint >= printInterval) {
          unsigned long intervals = (now - lastPrint) / printInterval;//computes how many update intervals have passed since the anchor. 
          lastPrint += intervals * printInterval; // advances the anchor forward by whole intervals (catch‑up) instead of jumping to now. That preserves a steady cadence and avoids drift when draws are slower or the loop missed some ticks.
          unsigned long remaining = (elapsed >= holdMs) ? 0 : (holdMs - elapsed);
          drawPopUp(remaining);//derived from the true elapsed time (accurate even if updates are delayed).
        }
      } else {
        // not yet reached popupDelay; ensure anchor is cleared so popup timing resets
        lastPrint = 0;
      }

        /*
        Why this is better than lastPrint = millis()

If you do lastPrint = millis() after each draw, any variable delay in the loop shifts the schedule and causes jitter or drift.
Using whole‑interval advances keeps periodic updates aligned to a consistent grid (e.g., 0, 200, 400 ms...) and prevents cumulative timing error.
Notes and edge cases

If the loop was very busy and intervals > 1, the code will advance the anchor by multiple intervals but perform a single draw (you can change it to draw multiple times if you want to catch up visually).
Use unsigned arithmetic (millis returns unsigned long) so subtraction handles wraparound safely.
Reset lastPrint (e.g., to 0) on press start and after the long action so the next press starts a fresh anchored schedule.
        */
      /*
      if (millis() - lastPrint >= printInterval) {//throttled by 200ms
        unsigned long remaining = holdMs - elapsed;
        //Serial.printf("Hold %lu ms more\n", remaining);
        drawPopUp(remaining);
        lastPrint = millis();
      }
      */
    }
  }


  // --- BOOT BUTTON (IO0) ---
  if (boot_button != lastBootState) lastBootDebounce = millis();
  
  if ((millis() - lastBootDebounce) > debounceDelay) {
    if (boot_button != stableBootState) {
      if (boot_button == LOW) { // Press
        bootPressStart = millis();
        bootLongHandled = false;
      } else { // Release
        unsigned long dur = (bootPressStart == 0) ? 0 : millis() - bootPressStart;
        if (!bootLongHandled && dur < 800) {
           // SHORT PRESS: ENTER DIR
           if (g_selectedIndex >= 0 && g_selectedIndex < (int)g_fileLines.size()) {
             if (g_fileIsDir[g_selectedIndex]) {
               // Derive directory name from displayed text instead of relying
               // on g_filePaths (which may be corrupted). Display format is
               // either "DIR: name" or "name  <size>", so extract the name.
               String display = g_fileLines[g_selectedIndex];
               String name;
               if (display.startsWith("DIR: ")) {
                 name = display.substring(5);
               } else {
                 int sep = display.indexOf("  ");
                 if (sep > 0) name = display.substring(0, sep);
                 else name = display;
               }

               // Compose full path from current path + name
               String targetPath = g_currentPath;
               if (!targetPath.endsWith("/")) targetPath += "/";
               targetPath += name;

               Serial.printf("Entering dir candidate: %s\n", targetPath.c_str());
               listFilesAndPrintSamples(targetPath.c_str());

              // reset selection to top
              g_selectedIndex = 0;
              }
           }
        }
        bootPressStart = 0;
        bootLongHandled = false;
      }
      stableBootState = boot_button;
    }
  }

  // LONG PRESS BOOT: GO UP
  if (stableBootState == LOW && bootPressStart != 0 && !bootLongHandled) {
    if (millis() - bootPressStart > 1000) {
      bootLongHandled = true;
      
      listFilesAndPrintSamples("/");
      Serial.println("User requested go to root (long-press BOOT)");
    }
  }

  lastButtonState = reading;
  lastBootState = boot_button;
  yield();
}
