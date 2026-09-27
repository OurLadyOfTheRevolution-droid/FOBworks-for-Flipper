# FOBworks for Flipper

Version 1.3. Official firmware 1.3.3 (FAP target 7, API 87.1).

`source/fobworks_flipper/dist/fobworks_flipper.fap` goes in `SD:/apps/Sub-GHz/`. Reboot before replacing an older FAP.

```
FOBscan    live capture
FOBclone   guided capture, two presses
FOBcatch   make, then listen; a decoded press stops it
FOBback    rollback profiles
FOBsweep   signal level
FOBprotos  protocol list
FOBLoq     built-in KeeLoq manufacturer keys
FOBwatch   receive and save
FOBlabs    edge timing
FOBhunt    RSSI sweep
FOBcrack   one KeeLoq frame, listed key or serial
Library    saved .sub files
Settings   frequency, modulation, squelch, force-protocol, dashboard link
FOBpwn     three Honda presses
```

The radio drops the first second after it opens. FOBcrack stops on the first KeeLoq frame and shows a listed manufacturer key or the serial. Ford on Auto is the V0 Manchester frame. The on-device clone list is a make and a frequency.

The dashboard link starts off. USB and pins 13 and 14 speak protocol 1.2. The Wi-Fi bridge is `source/fobworks_wifi_bridge/` (`FOBworks-Flipper`, `ws://192.168.4.1:81`).

GPL-3.0. Official firmware 1.3.3, API 87.1.
