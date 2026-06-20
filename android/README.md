# Kayak Tablet (Android)

Natywna aplikacja Android (tablet) — panel operacyjny ESP32 + mapa offline (v1).
Replikuje operacyjne funkcje web panelu firmware i dodaje mapę offline z pozycją łodzi.
Manualna jazda zostaje na nadajniku RC — aplikacja nie steruje gazem/skrętem.

Branch: `feature/android-tablet-app`

## Stack

- Kotlin + Jetpack Compose (Material3) + ViewModel/StateFlow
- OkHttp 5.x (REST + WebSocket), kotlinx.serialization (JSON)
- MapLibre Native (View-based, osadzony przez `AndroidView`) — Faza 4
- minSdk 29 (Android 10), targetSdk 34, compileSdk 34

### Dlaczego minSdk = 29

Ścieżka łączności (`requestNetwork` + `WifiNetworkSpecifier` bez `NET_CAPABILITY_INTERNET`)
wymaga API 29. Na API 29–32 specyfikator wymaga `ACCESS_FINE_LOCATION` (zadeklarowane
w manifeście z `maxSdkVersion="32"`). Na API 33+ uprawnienie lokalizacji nie jest potrzebne.

## Struktura

```
android/
  settings.gradle.kts, build.gradle.kts, gradle.properties
  gradle/libs.versions.toml          # pinowane wersje zależności
  app/
    build.gradle.kts
    src/main/AndroidManifest.xml
    src/main/java/com/thatmotor/kayak/
      MainActivity.kt
      net/    łączność z AP, REST, WS
      data/   modele + repozytorium telemetrii
      domain/ czyste funkcje (reducer, watchdog, konwersje) — testowane na JVM
      ui/     Compose UI + ViewModel + theme
      map/    MapLibre + warstwy offline + marker łodzi
    src/test/java/com/thatmotor/kayak/  # testy JVM
  maps/  README pipeline kafli offline (Faza 4)
```

## Build

Wymaga lokalnego Android SDK (`local.properties` z `sdk.dir`) i JDK 17.

```sh
cd android
./gradlew :app:assembleDebug      # APK debug
./gradlew :app:testDebugUnitTest  # testy JVM (logika domenowa)
```

> Uwaga: binarny `gradle/wrapper/gradle-wrapper.jar` nie jest commitowany w tym
> środowisku (brak Java/Gradle/SDK). Wygeneruj go raz lokalnie poleceniem
> `gradle wrapper --gradle-version 8.9` (wymaga zainstalowanego Gradle), albo
> otwórz projekt w Android Studio, które dociągnie wrapper automatycznie.

## Kontrakt API ESP32

Patrz `docs/active/android-tablet-app/android-tablet-app-kontekst.md` (źródło prawdy:
nagłówki firmware `components/web_panel/`). SoftAP WPA2-PSK, IP `192.168.4.1`,
telemetria `WS /ws` ~10 Hz, komendy `POST /api/command`.

## Licencje danych map (Faza 4)

OSM (wektor) — ODbL, atrybucja „© OpenStreetMap" widoczna w UI. Ortofoto — Geoportal
GUGiK ORTO WMTS (offline OK). NIE Mapbox/Google (ToS zabrania cache offline).
