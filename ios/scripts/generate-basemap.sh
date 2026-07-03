#!/usr/bin/env bash
# Regeneruje offline vector basemap (Protomaps Basemap v4) dla Mazowsza + Mazur
# (kraina Wielkich Jezior Mazurskich) jako bundlowy plik .pmtiles. Plik jest
# gitignorowany (~450 MB) — odtwórz go tym skryptem po świeżym clonie.
#
# Wymaga: pmtiles CLI (`brew install pmtiles`).
# Wycinek robiony przez range-requesty z hostowanego planet-builda Protomaps —
# nie pobiera całego planet.pmtiles.
set -euo pipefail

# Data planet-builda Protomaps. Builds znikają po pewnym czasie — jeśli 404,
# podaj aktualną: https://build.protomaps.com/<YYYYMMDD>.pmtiles
BUILD_DATE="${1:-20260630}"

# bbox=minLon,minLat,maxLon,maxLat — Mazowsze (od 51.0N) rozszerzone na północ do
# 54.3N, żeby objąć krainę Wielkich Jezior Mazurskich (Śniardwy, Mamry, Niegocin,
# Mikołajki, jeziora giżyckie i ełckie).
# maxzoom 13: woda pozostaje czysta dzięki predykatowi NOT(kind IN river/canal/
# stream) w LakeMapView (to on eliminuje earcut-"widma", nie natywny z14), więc
# z13 wystarcza i ~150 MB mniej niż z14. Przy większym przybliżeniu MapLibre
# nadpróbkuje kafle z13.
BBOX="19.2,51.0,23.2,54.3"
MAXZOOM="13"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$SCRIPT_DIR/../KayakMotor/Resources/mazowsze.pmtiles"

echo "Wycinam Mazowsze (bbox=$BBOX, maxzoom=$MAXZOOM) z buildu $BUILD_DATE…"
pmtiles extract "https://build.protomaps.com/${BUILD_DATE}.pmtiles" "$OUT" \
  --bbox="$BBOX" --maxzoom="$MAXZOOM"

echo "Gotowe: $OUT"
ls -lh "$OUT"
