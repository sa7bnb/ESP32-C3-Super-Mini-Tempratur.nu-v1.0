# 🌡️ Temperatur.nu Reporter

Firmware för ESP32-C3 som rapporterar temperatur till [Temperatur.nu](https://www.temperatur.nu/).

## Hårdvara

- ESP32-C3 Super Mini
- DS18B20 temperatursensor(er)
- 4.7kΩ resistor

### PCB (Valfritt)

Gerber-filer finns tillgängliga för att tillverka eget kretskort via [JLCPCB.com](https://jlcpcb.com/) eller annan PCB-tillverkare.

## Koppling
ESP32-C3    DS18B20

GPIO4  ──── DQ (Data)
3.3V   ──── VDD
GND    ──── GND
4.7kΩ resistor mellan GPIO4 och 3.3V

Flera sensorer kan kopplas parallellt på samma GPIO4.

## Installation

1. Installera Arduino IDE med ESP32 support
2. Installera bibliotek: `OneWire` och `DallasTemperature`
3. Välj board: **ESP32C3 Dev Module**
4. Ladda upp koden

## Användning

### Första gången
1. Enheten skapar WiFi: **TEMP-Setup** (lösenord: `12345678`)
2. Anslut och gå till `http://192.168.4.1`
3. Konfigurera ditt WiFi
4. Hitta enhetens IP i routern (söker efter `temperatur_nu_*`)
5. Kopiera PIN-koden från webbsidan
6. Registrera på [temperatur.nu/nystation](https://www.temperatur.nu/nystation/)

### Funktioner
- ✅ Automatisk rapportering var 3:e minut
- ✅ Stöd för flera sensorer (rapporterar lägsta temperatur)
- ✅ Webbgränssnitt för status och konfiguration
- ✅ Återställ WiFi via webben

## Felsökning

**Serial Monitor:** 115200 baud för debug-information

**Kan inte hitta sensorer:** Kontrollera koppling och pull-up resistor

**WiFi problem:** Använd "Återställ WiFi" från webbgränssnittet

---

Utvecklad av A. Isaksson 2025
