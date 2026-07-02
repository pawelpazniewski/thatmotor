#!/usr/bin/env bash
# Regeneruje offline vector basemap (Protomaps Basemap v4) dla Mazowsza jako
# bundlowy plik .pmtiles. Plik jest gitignorowany (188 MB) — odtwórz go tym
# skryptem po świeżym clonie.
#
# Wymaga: pmtiles CLI (`brew install pmtiles`).
# Wycinek robiony przez range-requesty z hostowanego planet-builda Protomaps —
# nie pobiera całego planet.pmtiles.
set -euo pipefail

# Data planet-builda Protomaps. Builds znikają po pewnym czasie — jeśli 404,
# podaj aktualną: https://build.protomaps.com/<YYYYMMDD>.pmtiles
BUILD_DATE="${1:-20260630}"

# bbox=minLon,minLat,maxLon,maxLat — województwo mazowieckie.
# maxzoom 14: na z13 i niżej Protomaps generalizuje wodę w multipoligony, które
# earcut MapLibre tesseluje w artefakty ("widma"). Natywny z14 jest czysty, więc
# woda w apce rysowana jest dopiero od z14 (LakeMapView).
BBOX="19.2,51.0,23.2,53.5"
MAXZOOM="14"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$SCRIPT_DIR/../KayakMotor/Resources/mazowsze.pmtiles"

echo "Wycinam Mazowsze (bbox=$BBOX, maxzoom=$MAXZOOM) z buildu $BUILD_DATE…"
pmtiles extract "https://build.protomaps.com/${BUILD_DATE}.pmtiles" "$OUT" \
  --bbox="$BBOX" --maxzoom="$MAXZOOM"

echo "Gotowe: $OUT"
ls -lh "$OUT"
