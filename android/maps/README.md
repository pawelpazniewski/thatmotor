# Offline map pipeline (Unit 11)

How to produce the two offline archives the app renders (Unit 8 / Unit 9):

- **OSM vector** → `osm.pmtiles` (PMTiles), bundled in `app/src/main/assets/maps/`.
- **Geoportal ortho raster** → `ortho.mbtiles` (MBTiles), extracted to app internal storage
  at runtime (MBTiles cannot be read from an APK asset).

Both archives are large binaries and are **git-ignored** (`*.pmtiles`, `*.mbtiles`, see
`android/.gitignore`). Only generate the bbox of the actual operating water — do not tile
whole regions.

> Set the operating-area bounding box once and reuse it for both pipelines so the vector and
> raster cover exactly the same extent.
>
> ```sh
> # min_lon,min_lat,max_lon,max_lat  (example — replace with your water)
> export BBOX="20.90,52.20,21.10,52.35"
> export MINZOOM=10
> export MAXZOOM=16
> ```

## 1. OSM vector → PMTiles (Planetiler)

[Planetiler](https://github.com/onthegomap/planetiler) builds a vector tileset from an OSM
extract. Download only the region you need from [Geofabrik](https://download.geofabrik.de/)
(country/state extract), then clip to the bbox.

```sh
# 1. Get a regional OSM extract (.osm.pbf) — pick the smallest that covers your water.
#    e.g. https://download.geofabrik.de/europe/poland/mazowieckie-latest.osm.pbf

# 2. Build PMTiles directly for the bbox.
java -Xmx4g -jar planetiler.jar \
  --osm-path=mazowieckie-latest.osm.pbf \
  --bounds=$BBOX \
  --minzoom=$MINZOOM --maxzoom=$MAXZOOM \
  --output=osm.pmtiles

# 3. Place it where the APK bundles assets.
cp osm.pmtiles ../app/src/main/assets/maps/osm.pmtiles
```

The `source-layer` names in `OfflineStyle.buildOfflineStyleJson` (`background-fill`,
`water`, `transportation`) follow Planetiler's default OpenMapTiles schema — adjust the
style layers if you use a different schema.

## 2. Geoportal ORTO (WMTS) → MBTiles (GDAL / rio-mbtiles)

Poland's [Geoportal ORTO WMTS](https://mapy.geoportal.gov.pl/) serves orthophoto raster
tiles. Two equivalent paths:

### Option A — GDAL (`gdal_translate` + `gdaladdo`)

```sh
# 1. Describe the WMTS layer as a GDAL source (cache the GetCapabilities XML locally).
#    https://mapy.geoportal.gov.pl/wss/service/PZGIK/ORTO/WMTS/StandardResolution
gdal_translate \
  -projwin_srs EPSG:4326 \
  -projwin <min_lon> <max_lat> <max_lon> <min_lat> \
  -of MBTILES \
  -co TILE_FORMAT=JPEG -co QUALITY=85 \
  ORTO_wmts.xml ortho.mbtiles

# 2. Build overviews (zoom pyramid) so lower zooms render.
gdaladdo -r average ortho.mbtiles 2 4 8 16
```

### Option B — `rio-mbtiles` (rasterio CLI)

```sh
# From a downloaded/warped GeoTIFF of the bbox:
rio mbtiles ortho_bbox.tif ortho.mbtiles \
  --zoom-levels $MINZOOM..$MAXZOOM --format JPEG -j 4
```

The resulting `ortho.mbtiles` is **not** bundled as an APK asset — ship it out-of-band (or
fetch once) and have the app copy it into `filesDir` as `ortho.mbtiles`
(`MapAssets.ORTO_MBTILES_FILE`); the style points `mbtiles://` at that absolute path
(`MapAssets.sources`).

### Rate limiting (mandatory)

Geoportal WMTS is a shared public service. When tiling:

- request **only the operating bbox**, never a whole voivodeship/country;
- cap concurrency (e.g. GDAL `--config GDAL_HTTP_MAX_RETRY 3 GDAL_HTTP_RETRY_DELAY 2`,
  `rio mbtiles -j 4` at most);
- insert a delay between batches and reuse a local tile cache so re-runs do not re-hit the
  server;
- run tiling **once** and commit the build recipe (this file), not by re-downloading per build.

## Attribution & licensing

- **OSM vector** — © OpenStreetMap contributors, **ODbL**. The credit string
  (`OfflineStyle.OSM_ATTRIBUTION`) is wired into the vector source and shown on the map.
- **Geoportal ORTO** — Polish public geodata; credit *Główny Urząd Geodezji i Kartografii
  (GUGiK)* per the Geoportal terms of use. Keep both credits visible in the app's map
  attribution area.

## Asset vs internal storage (summary)

| Archive          | Format  | Lives in                         | URI scheme                          |
|------------------|---------|----------------------------------|-------------------------------------|
| OSM vector       | PMTiles | APK `assets/maps/`               | `pmtiles://asset://maps/osm.pmtiles`|
| Geoportal ortho  | MBTiles | app internal storage (`filesDir`)| `mbtiles://<absolute path>`         |
| sprite / glyphs  | png/pbf | APK `assets/style/`              | `asset://style/...`                 |

PMTiles vector sources require **MapLibre Android >= 11.7.0** (pinned in
`gradle/libs.versions.toml`).
