/*
   This is sample sketch that demonstrates
    * the use of RTC memory to keep information over reset / deep sleep
    * fast WiFi connection when the bssid and channel of the access point
      is already known.

    The sketch defines therefore a struct rtcMemory with all data that
    should be saved over reset / deep sleep and uses a crc32 checksum, to
    detect if the RTC memory content is valid.
    This struct is then mapped directly to the RTC memory.

    WARNING! Access to the RTC memory has to be 4-byte aligned. Unaligned
    access results in wrong data and / or chrashes!

    After successfully connection to an access point, it's channel, bssid,
    ip, netmask, gateway, dns adresses (ipv4 only) is saved into the RTC
    memory and the crc is updated.

    In loop, the sketch waits for an 'S' on Serial to enter deep sleep for
    10 seconds or a 'C' to clear the RTC memory.
*/

#include <ESP8266WiFi.h>
#include <coredecls.h>         // a crc32() is defined here

/*
   Direct access to the RTC memory *must* be 4-byte aligned!
   If not, the program might crash (exception) or produce
   invalid data written to the RTC!
*/
struct __attribute__ ((align(4))) rtcMemory
{
  rtcMemory()
  {
    // make sure, we don't exceed the rtc memory
    static_assert(sizeof(rtcMemory) <= 512, "Your RTC memory structure is too large. You can use 512 bytes at max!");
  }

  uint32_t resetCount;     // reset counter
  uint32_t sleepCount;     // deep sleep counter
  int32_t channel;         // last channel
  uint8_t BSSID[6];        // last bssid
  uint32_t localIP;        // last ipv4 data
  uint32_t gateway;
  uint32_t subnet;
  uint32_t dns1;
  uint32_t dns2;
  uint32_t crc;            // crc of the data above

  bool isValid() const
  {
    return crc == crc32( (const uint8_t*)this, sizeof(*this) - sizeof(crc) );
  }

  void makeValid()
  {
    crc = crc32( (const uint8_t*)this, sizeof(*this) - sizeof(crc) );
  }

  void clear()
  {
    // this will also invalidate as the crc of 00 00 ... is not 0!
    memset(this, 0, sizeof(*this));
  }

  static void memcpy_align4(void* _dst, const void* _src, uint16_t len)
  {
    /*
       copy len bytes from _src to _dst making sure that each byte is
       read/written on a 4-byte alignement.
    */
    uint8_t* dst = (uint8_t*)_dst;
    const uint8_t* src = (uint8_t*)_src;
    while (len--)
    {
      // mask out destination byte
      const uint32_t dmask = 0xFF << ((uint32_t(dst) & 3) << 3);
      (*(uint32_t*)(uint32_t(dst) & ~3)) &= ~dmask;

      // read source byte
      uint8_t tmp = (*(uint32_t*)(uint32_t(src) & ~3))  >> ((uint32_t(src) & 3) << 3);

      // put into destination place
      (*(uint32_t*)(uint32_t(dst) & ~3)) |= tmp << ((uint32_t(dst) & 3) << 3);

      ++dst;
      ++src;
    }
  }

  static void printMemory(volatile void *data, uint16_t len)
  {
    /*
       hexdump a piece of memory
    */
    volatile uint8_t* ptr = (volatile uint8_t*)data;
    for (uint16_t i = 0; i < len; i++)
    {
      Serial.printf_P(PSTR("%02X%c"), ptr[i], (i + 1) % 32 == 0 ? '\n' : ' ');
    }
    Serial.print('\n');
  }
};

// map rtcMemory struct directly into the RTC memory
static rtcMemory* rtc = (rtcMemory*)(RTC_USER_MEM);

// sleep how many seconds
const uint32_t DEEP_SLEEP_TIME = 10;

// your WiFi credentials go here!
const char *ssid PROGMEM = "YOUR_SSID";
const char *psk PROGMEM = "YOUR_PSK";


void setup(void)
{
  uint32_t mStart = millis();

  Serial.begin(74880);

  rst_info *ri = ESP.getResetInfoPtr();
  Serial.printf_P(PSTR("\n\n\nEnter Setup @ %u ms, reason: %s reset (%d)\n"), mStart, ESP.getResetReason().c_str(), ri->reason);

  // don't wear out flash: don't save ssid/psk on every WiFi connect
  WiFi.persistent(false);

  if (!rtc->isValid())
  {
    rtc->clear();
    Serial.printf_P(PSTR("RTC cleared, just connecting to %S/%S"), ssid, psk);
    WiFi.begin(ssid, psk);
  }
  else
  {
    if (ri->reason == REASON_DEEP_SLEEP_AWAKE)
    {
      ++rtc->sleepCount;
    }
    else
    {
      ++rtc->resetCount;
    }

    IPAddress _local = rtc->localIP;
    IPAddress _gw = rtc->gateway;
    IPAddress _sub = rtc->subnet;
    IPAddress _dns1 = rtc->dns1;
    IPAddress _dns2 = rtc->dns2;

    Serial.printf_P(PSTR("RTC valid, connecting to %S using\n  ip: %s/%s, gw: %s, dns: %s/%s, channel %d and BSSID "), ssid,
                    _local.toString().c_str(), _sub.toString().c_str(), _gw.toString().c_str(), _dns1.toString().c_str(), _dns2.toString().c_str(), rtc->channel);
    rtcMemory::printMemory(rtc->BSSID, 6);

    WiFi.config(_local, _gw, _sub, _dns1, _dns2);
    WiFi.begin(ssid, psk, rtc->channel, rtc->BSSID);
  }


  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(5);
    if (millis() % 1000 < 5) Serial.printf_P(PSTR("."));
  }

  uint32_t mEnd = millis();
  Serial.printf_P(PSTR("\n\nTook %u-%u = ***%u ms*** to connect!\nConnected to %S with BSSID = %s and channel=%d, our IP address is %s\n"), mEnd, mStart, mEnd - mStart, ssid, WiFi.BSSIDstr().c_str(), WiFi.channel(), WiFi.localIP().toString().c_str());

  // save all connection data
  rtc->channel = WiFi.channel();
  rtcMemory::memcpy_align4(&rtc->BSSID, WiFi.BSSID(), 6);
  rtc->localIP = WiFi.localIP();
  rtc->gateway = WiFi.gatewayIP();
  rtc->subnet = WiFi.subnetMask();
  rtc->dns1 = WiFi.dnsIP(0);
  rtc->dns2 = WiFi.dnsIP(1);

  // update crc
  rtc->makeValid();

  // show the contents of the RTC memory
  Serial.printf_P(PSTR("\nRTC memory content is now:\n"));
  rtcMemory::printMemory(rtc, sizeof(*rtc));
}

void loop()
{
  // wait for an 'S' to enter deep sleep mode or an 'C' to clear the RTC memory
  if (Serial.available())
  {
    char c = Serial.read();
    if ('S' == c)
    {
      Serial.printf_P(PSTR("%u resets, %u deep sleeps so far, entering deep sleep mode for 10 sec..."), rtc->resetCount, rtc->sleepCount);
      ESP.deepSleep(DEEP_SLEEP_TIME * 1000000);
    }
    else if ('C' == c)
    {
      rtc->clear();
      Serial.printf_P(PSTR("\nRTC memory cleared, it's content is now:\n"));
      rtcMemory::printMemory(rtc, sizeof(*rtc));
    }
  }
  delay(10);
}
