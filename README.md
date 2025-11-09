# Glitch Clocks

ESP32 sketch that drives up to four clock sync lanes for DC 24V 1-minute reverse impulse slave clocks using two L298N H-bridges. It acts like a master clock but instead of accurate timekeeping, it is intended for art installation use. Each of the clock lines can support multiple clocks and runs a base tempo with small variantions and occasional glitch effects.

Depending on the power, it's estimated that ~80 clocks in total would still be feasible (and awesome), but I'm using only three clocks in my installation.

The art piece was first presented in [Reformi Art Arkkivisio](https://www.reformi.art/en) urban arts exhibition in 2025. It's a part of a series of my ongoing open source art project dubbed CTRL-ART-DEL.

## Video and pics

(TBD)

## Hardware

- ESP32 dev board
- 2x L298N (or similar) H-bridge
- Slave clocks
- 24V power supply for the clock coils

See [Glitch Clock schematics in Cirkit Designer](https://app.cirkitdesigner.com/project/c464c2bb-df0a-4651-a865-9290406890fa) and my blog article for complete hardware and build reference https://medium.com/@ville.alatalo/kello-joka-tarvitsee-kellon-pysyäkseen-ajassa-4ecf6e186a1d (in Finnish).

![Glitch Clock schematics in Cirkit Designer](/glitch-clocks-schematics.png)

## Configure and build

Top part of the sketch file contains controls base tempos, pulse timing and effect specific settings. It also has configuration for the pin map and number of clock sync lanes.

Build and upload using Arduino IDE.

## Serial monitoring

Effect changelog is show in in the serial monitor.

```sh
Glitch Clocks
[accel] to x1.25 over 10000ms
[accel] done
[reorg] tempos now A=0.98 B=1.02 C=1.01 D=1.00
[decel] to x0.81 over 10000ms
[decel] done
[stagger] window 3500 ms, step 700 ms
[stagger] resume
[snap] B follows C
[snap] release
[sync] lock @ 120 BPM for 8000ms
[sync] release
[glitch] B freeze 2975ms
[storm] 3500ms window
[storm] end
[scope] +0.010 Hz over 60000ms
[pause] long silence 11671 ms
[pause] resume
```

## Links

- [Phasing clocks "simulator"](https://github.com/alatalo/phasing-clocks)
- [Clock that needs another clock for sync, Medium blog article in Finnish about master and slave clocks](https://medium.com/@ville.alatalo/kello-joka-tarvitsee-kellon-pysyäkseen-ajassa-4ecf6e186a1d)
- [ESP32 NTP master clock gist](https://gist.github.com/alatalo/cdd7e4c48bdc5a3fbb875d33d4a6d590)

## Attribution

The code is written using ChatGPT 5.

## License

MIT. See `LICENSE`.
