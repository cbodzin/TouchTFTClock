# TouchTFTClock

A simple NTP-enabled clock based on the ESP32-2432S028R.

Fill out `settings.txt` and put it in the root directory of your SD card.  It uses the following `key:value` pairs:

* ssid:`your ssid`
* password:`your password`
* tz:`-4` _(UTC offset, i.e. -4 for US ET)_
* dst:`0` _(0 for no DST, 1 for DST)_
* night:`21` _(the hour to switch to Big Clock)_
* day:`8` _(the hour to switch off Big Clock)_
