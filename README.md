# Glitch Clocks

ESP32 sketch that drives up to four clock sync lanes for DC 24V 1-minute reverse impulse slave clocks using two L298N H-bridges. It acts like a [master clock](https://gist.github.com/alatalo/cdd7e4c48bdc5a3fbb875d33d4a6d590) but instead of accurate timekeeping, it is intended for art installation use. Each of the clock lanes can support multiple clocks and runs at a base tempo with small variations and occasional glitch effects.

Depending on the power, it's estimated that ~80 slave clocks in total would still be feasible (and awesome), but I was only using three clocks and three separate clock lanes in my installation at the time.

The art piece was first presented in [Reformi Art Arkkivisio](https://www.reformi.art/en) urban arts exhibition in 2025. It's a part of a series of my ongoing open source art project dubbed CTRL-ART-DEL.

## Video and pics

Video (click to open in YouTube):

[![Glitch Clocks in the Reformi Art Arkkivisio exhibition](/media/glitch-clocks-video.jpg)](https://youtu.be/B35jJOndVuE "Glitch Clocks in the Reformi Art Arkkivisio exhibition")

Three slave clocks connected to the master clock:

![Glitch Clocks in the Reformi Art Arkkivisio exhibition](/media/glitch-clocks-01.jpg)

The master clock logic was built inside an old central radio unit:

![Glitch Clocks in the Reformi Art Arkkivisio exhibition](/media/glitch-clocks-02.jpg)

## Hardware

- ESP32 dev board
- 2x L298N (or similar) H-bridge
- 24V power supply for the clock coils
- Slave clocks

See [Glitch Clock schematics in Cirkit Designer](https://app.cirkitdesigner.com/project/c464c2bb-df0a-4651-a865-9290406890fa).

My blog article for complete hardware and build reference https://medium.com/@ville.alatalo/kello-joka-tarvitsee-kellon-pysyäkseen-ajassa-4ecf6e186a1d (in Finnish).

![Glitch Clock schematics in Cirkit Designer](/media/glitch-clocks-schematics.png)

## Configure and build

Top part of the sketch file contains controls base tempos, pulse timing and effect specific settings. It also has configuration for the pin map and number of clock sync lanes.

Build and upload using Arduino IDE.

```c++
struct LanePins {
  int IN1, IN2, EN;
};
const LanePins LANE_A_PINS = { 33, 14, 13 };
const LanePins LANE_B_PINS = { 27, 26, 25 };
const LanePins LANE_C_PINS = { 19, 21, 18 };
const LanePins LANE_D_PINS = { 22, 23, 32 };
```

## Serial monitoring

Effect changelog is show in in the serial monitor.

```md
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

- [Glitch Clock schematics in Cirkit Designer](https://app.cirkitdesigner.com/project/c464c2bb-df0a-4651-a865-9290406890fa)
- [Phasing clocks web browser "simulator"](https://github.com/alatalo/phasing-clocks)
- [Clock that needs another clock for sync; Medium blog article in Finnish about master and slave clocks](https://medium.com/@ville.alatalo/kello-joka-tarvitsee-kellon-pysyäkseen-ajassa-4ecf6e186a1d)
- [ESP32 NTP master clock gist](https://gist.github.com/alatalo/cdd7e4c48bdc5a3fbb875d33d4a6d590)

## Attribution

The code is written using ChatGPT 5.

## License

MIT. See [LICENSE](/LICENSE).
