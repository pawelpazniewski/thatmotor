# Offline map style assets

Files MapLibre loads from the APK `assets/` directory at runtime.

- `style.json` — committed reference style mirroring `OfflineStyle.buildOfflineStyleJson`.
  The app builds the live style in code (so URIs can be resolved against internal storage),
  but this static copy documents the expected source/layer shape and can be loaded directly
  via `asset://style/style.json` for quick inspection.
- `sprite.json` / `sprite.png` (and `@2x` variants) — the sprite sheet that provides the
  `boat` marker icon referenced by `BoatMarker`. These are **generated binaries** produced by
  the map pipeline (see `android/maps/README.md`); they are not hand-edited here.
- `glyphs/{fontstack}/{range}.pbf` — font glyph ranges for label rendering, also generated.

The PMTiles vector archive lives at `assets/maps/osm.pmtiles` (loaded via
`pmtiles://asset://maps/osm.pmtiles`). The ortho MBTiles raster cannot be read straight from
an APK asset, so the pipeline extracts it to app internal storage and the style points
`mbtiles://` at that filesystem path. Both archives are large and are git-ignored
(`*.pmtiles`, `*.mbtiles`).
