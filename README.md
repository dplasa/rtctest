# rtctest
Arduino ESP8266 sketch demonstrating fast WiFi connection with BSSID/channel saved in RTC memory.

## Purpose 
This is sample sketch that demonstrates
 * the use of RTC memory to keep information over reset / deep sleep
 * fast WiFi connection when the bssid and channel of the access point is already known.

## How it works
The sketch defines a struct ``rtcMemory`` with all data that should be saved over reset / deep sleep and uses a crc32 checksum, to detect if the RTC memory content is valid:

```cpp
struct __attribute__ ((align(4))) rtcMemory
{
  rtcMemory()
  {
    // make sure, we don't exceed the rtc memory
    static_assert(sizeof(rtcMemory) <= 512, "Your RTC memory structure is too large. You can use 512 bytes at max!");
  }

  uint32_t resetCount;     // reset counter
  ....
};
```

This struct is then mapped directly to the RTC memory:
```cpp
static rtcMemory* rtc = (rtcMemory*)(RTC_USER_MEM);
```


**WARNING!** Access to the RTC memory has to be 4-byte aligned. Unaligned
access results in wrong data and / or chrashes!

After a reset (i.e. ``setup()`` gets called), the sketch determines if the content of the RTC memory is valid. 

* If the it is **not valid**, the sketch just tries to connect using a given ssid/psk combination, trying to obtain an ip address via dhcp. After a successful connection to an access point, it's channel, bssid, ip, netmask, gateway, dns adresses (ipv4 only) is saved into the RTC memory and the crc is updated.

* If the content of the RTC memory however **is valid**, the WiFi gets configured with the previously saved ip, netmask, ... and the connecion is set up with the specific channel and bssid. This speeds up the connection process drastically.

## Results
Using WPA2 PSK, the connection normally takes around 3 seconds to be established. If you provide the chnannel and bssid this comes down to 1 second. Speedup by 3, not bad ;-)

If you even can (**be sure not to jeopardize your security here**) drop encryption and just use an unencrypted connection, you will be able to conenct within ~0.1-0.2 seconds. Speedup by 20-30, really not bad at all! 

## Use case
If your eps8266 runs on battery to provide sensor data (e.g. weather data with an BME280 sensor) the time you can run on battery is almost not determined by the time spent in deep sleep mode but by time in the active mode, when the sensor data is being sent. And again this time is almost entirely determined by the time spent in establishing the WiFi connection itself.
Reducing the connection setup time greatly improves your battery time!  

## Credits
Kudos to Thorsten von Eicken who did a comprehensive analysis of the esp8266 WiFi connection and how connection time is affected by several parameters. Visit his Blog https://blog.voneicken.com/ and read the full article https://blog.voneicken.com/2018/lp-wifi-esp8266-1/ !

## What else to say
In loop, the sketch waits for an 'S' on Serial to enter deep sleep for 10 seconds or a 'C' to clear the RTC memory. A sleep counter and a reset counter will be inceremented and displayed.