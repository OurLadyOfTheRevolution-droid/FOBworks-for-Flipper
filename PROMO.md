# FOBworks for Flipper

This is version 1.3, as recorded in the source manifest and bundled FAP. The
upstream release notes report a successful launch on official firmware 1.4.3
(FAP target 7, API 87.1). A FAP loads only on an exact API match, so this build
runs on 1.4.2 and 1.4.3, not on 1.3.x, whose API is 86.0.

Copy `source/fobworks_flipper/dist/fobworks_flipper.fap` to
`SD:/apps/Sub-GHz/`. Reboot before replacing an older FAP.

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

Receivers skip the first second after opening the radio. FOBcrack stops at the
first KeeLoq frame; it shows a listed manufacturer key if one matches, or the
serial otherwise. Ford on the Auto path uses the V0 Manchester frame. The
on-device clone list contains a make and frequency.

The dashboard link is off by default. USB and GPIO pins 13 and 14 use protocol
1.2. The optional Wi-Fi bridge is in `source/fobworks_wifi_bridge/`; it creates
the `FOBworks-Flipper` access point and accepts WebSocket connections at
`ws://192.168.4.1:81`.

Licensed under GPL-3.0. The upstream release notes report verification with
official firmware 1.4.3, API 87.1.
