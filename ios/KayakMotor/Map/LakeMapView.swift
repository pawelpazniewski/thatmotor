import SwiftUI
import MapLibre
import CoreLocation
import KayakContract

/// Stan łodzi do renderu markera.
struct BoatRenderState: Equatable {
    var coordinate: CLLocationCoordinate2D?
    var headingDegrees: Double

    static func == (lhs: BoatRenderState, rhs: BoatRenderState) -> Bool {
        lhs.coordinate?.latitude == rhs.coordinate?.latitude &&
        lhs.coordinate?.longitude == rhs.coordinate?.longitude &&
        lhs.headingDegrees == rhs.headingDegrees
    }
}

/// Mapa offline (Unit 5/6): bundlowy blank-style + kontur akwenu z `lake.geojson`,
/// marker łodzi (`MLNSymbolStyleLayer` z `iconRotation`), pin celu + linia łódź→cel.
/// Aktualizacja markerów przez podmianę `MLNShapeSource.shape` (bufor GPU). Zero sieci.
/// Most między przyciskami SwiftUI a `MLNMapView` do sterowania zoomem. Trzyma
/// słabą referencję do mapy (ustawianą w `makeUIView`), by nie tworzyć retain cycle.
@MainActor
final class MapCameraController {
    fileprivate weak var mapView: MLNMapView?

    func zoomIn() { step(by: 1) }
    func zoomOut() { step(by: -1) }

    /// Wyśrodkuj widok na „Tobie": łódka (fix z telemetrii) jeśli dostępna, inaczej
    /// pozycja telefonu (GPS). Zachowuje bieżący zoom. Nic nie robi bez żadnej pozycji.
    func recenter(boat: CLLocationCoordinate2D?) {
        guard let mapView else { return }
        guard let target = boat ?? mapView.userLocation?.location?.coordinate else { return }
        mapView.setCenter(target, animated: true)
    }

    private func step(by delta: Double) {
        guard let mapView else { return }
        mapView.setZoomLevel(mapView.zoomLevel + delta, animated: true)
    }
}

struct LakeMapView: UIViewRepresentable {
    var boat: BoatRenderState
    var target: CLLocationCoordinate2D?
    var camera: MapCameraController
    var followsBoat: Bool = true
    var onTap: (CLLocationCoordinate2D) -> Void = { _ in }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeUIView(context: Context) -> MLNMapView {
        let mapView = MLNMapView(frame: .zero)
        mapView.styleURL = Bundle.main.url(forResource: "blank-style", withExtension: "json")
        mapView.delegate = context.coordinator
        mapView.logoView.isHidden = true
        mapView.allowsZooming = true
        mapView.allowsRotating = true
        // Niebieska kropka „tu jestem" (pozycja telefonu). MapLibre sam prosi o
        // uprawnienie i zarządza CLLocationManager. Tryb .none — kamera nadal śledzi
        // łódkę; pozycja telefonu to wyłącznie odniesienie, nie wejście nawigacji.
        mapView.showsUserLocation = true
        // Domyślny widok: kraina Wielkich Jezior Mazurskich (Śniardwy/Mikołajki/
        // Niegocin) — realny obszar pływania. Kamera i tak przeskoczy na łódkę,
        // gdy tylko pojawi się pozycja z GPS.
        mapView.setCenter(CLLocationCoordinate2D(latitude: 53.83, longitude: 21.65),
                          zoomLevel: 13, animated: false)
        camera.mapView = mapView

        let tap = UITapGestureRecognizer(target: context.coordinator,
                                         action: #selector(Coordinator.handleTap(_:)))
        mapView.addGestureRecognizer(tap)
        return mapView
    }

    func updateUIView(_ mapView: MLNMapView, context: Context) {
        context.coordinator.onTap = onTap
        context.coordinator.updateBoat(boat, on: mapView, follow: followsBoat)
        context.coordinator.updateTarget(target, boat: boat.coordinate, on: mapView)
    }

    final class Coordinator: NSObject, MLNMapViewDelegate {
        private static let boatSourceID = "boat-src"
        private static let targetSourceID = "target-src"
        private static let routeSourceID = "route-src"
        private static let boatIconName = "boat-icon"
        private static let targetIconName = "target-icon"
        private var lastCameraMove = Date.distantPast
        private var hasBoatFix = false
        private var hasCenteredOnUser = false
        var onTap: (CLLocationCoordinate2D) -> Void = { _ in }

        @objc func handleTap(_ recognizer: UITapGestureRecognizer) {
            guard let mapView = recognizer.view as? MLNMapView else { return }
            let point = recognizer.location(in: mapView)
            onTap(mapView.convert(point, toCoordinateFrom: mapView))
        }

        func mapView(_ mapView: MLNMapView, didFinishLoading style: MLNStyle) {
            addBasemap(to: style)
            addRouteLine(to: style)
            addBoatLayer(to: style)
            addTargetLayer(to: style)
        }

        /// Zanim silnik złapie fix, kamera stoi na domyślnym środku — wyśrodkuj więc
        /// jednorazowo na pozycji telefonu, by kropka „tu jestem" była widoczna.
        /// Po pojawieniu się pozycji łódki `updateBoat` przejmuje kamerę.
        func mapView(_ mapView: MLNMapView, didUpdate userLocation: MLNUserLocation?) {
            guard !hasCenteredOnUser, !hasBoatFix,
                  let location = userLocation?.location else { return }
            mapView.setCenter(location.coordinate, zoomLevel: 15, animated: true)
            hasCenteredOnUser = true
        }

        /// Offline vector basemap (Protomaps Basemap v4) z bundlowego `.pmtiles`.
        /// Bez etykiet (brak glyphs/sprite w bundlu) — ląd/woda/zieleń/drogi. Ścieżkę
        /// bundla resolwujemy w runtime (`file://` z UUID zmiennym po reinstalacji),
        /// stąd `pmtiles://` + `configurationURL`, a nie ścieżka w statycznym stylu.
        private func addBasemap(to style: MLNStyle) {
            guard let fileURL = Bundle.main.url(forResource: "mazowsze", withExtension: "pmtiles"),
                  let sourceURL = URL(string: "pmtiles://\(fileURL.absoluteString)") else { return }
            let source = MLNVectorTileSource(identifier: "basemap", configurationURL: sourceURL)
            style.addSource(source)

            // Cały ląd na jednolitą jasną zieleń — teren bez poligonu landuse (pola,
            // nieokryty grunt) NIE odcina się wtedy jasną plamą od roślinności (to był
            // mylący „ukośny pas": goły `earth` między zielenią, np. dolina rzeki).
            let earth = MLNFillStyleLayer(identifier: "basemap-earth", source: source)
            earth.sourceLayerIdentifier = "earth"
            earth.fillColor = NSExpression(forConstantValue: UIColor(red: 0.86, green: 0.89, blue: 0.79, alpha: 1))
            style.addLayer(earth)

            // Tereny zabudowane/przemysłowe — neutralna szarość, odróżnia od zieleni.
            let developed = MLNFillStyleLayer(identifier: "basemap-developed", source: source)
            developed.sourceLayerIdentifier = "landuse"
            developed.predicate = NSPredicate(format: "kind IN %@",
                ["residential", "industrial", "commercial", "retail", "military",
                 "railway", "aerodrome", "quarry", "school", "university", "hospital"])
            developed.fillColor = NSExpression(forConstantValue: UIColor(red: 0.84, green: 0.82, blue: 0.78, alpha: 1))
            style.addLayer(developed)

            // Las — ciemniejsza zieleń, TYLKO realny las (`forest`/`wood`). Granic
            // obszarów chronionych (`national_park`/`nature_reserve`) NIE wypełniamy:
            // obejmują też pola i wsie, więc jednolite wypełnienie robiło wielki
            // ciemnozielony „blob" wyglądający jak nakładka. Las w parku i tak się rysuje.
            let woodland = MLNFillStyleLayer(identifier: "basemap-wood", source: source)
            woodland.sourceLayerIdentifier = "landuse"
            woodland.predicate = NSPredicate(format: "kind IN %@", ["forest", "wood"])
            woodland.fillColor = NSExpression(forConstantValue: UIColor(red: 0.59, green: 0.75, blue: 0.55, alpha: 1))
            style.addLayer(woodland)

            // Wypełnienie wody na wszystkich zoomach, ale TYLKO zwarte akweny (jeziora,
            // zbiorniki, Wisła jako `water`, stawy). Wykluczamy `river`/`canal`/`stream`:
            // to wydłużone, źle domknięte wielokąty, które earcut MapLibre „rozlewa"
            // w kliny/„widma". `water` tesseluje się czysto na każdym zoomie (zweryfikowane).
            let water = MLNFillStyleLayer(identifier: "basemap-water", source: source)
            water.sourceLayerIdentifier = "water"
            water.predicate = NSPredicate(format: "NOT (kind IN %@)", ["river", "canal", "stream"])
            water.fillColor = NSExpression(forConstantValue: UIColor(red: 0.42, green: 0.65, blue: 0.82, alpha: 1))
            style.addLayer(water)

            let roads = MLNLineStyleLayer(identifier: "basemap-roads", source: source)
            roads.sourceLayerIdentifier = "roads"
            roads.predicate = NSPredicate(format: "kind IN %@", ["highway", "major_road", "medium_road"])
            roads.lineColor = NSExpression(forConstantValue: UIColor(white: 0.6, alpha: 1))
            roads.lineWidth = NSExpression(forConstantValue: 1.2)
            style.addLayer(roads)
        }

        private func addRouteLine(to style: MLNStyle) {
            let source = MLNShapeSource(identifier: Self.routeSourceID, shape: nil, options: nil)
            style.addSource(source)
            let line = MLNLineStyleLayer(identifier: "route-line", source: source)
            line.lineColor = NSExpression(forConstantValue: UIColor.systemYellow)
            line.lineWidth = NSExpression(forConstantValue: 2)
            line.lineDashPattern = NSExpression(forConstantValue: [2, 2])
            style.addLayer(line)
        }

        private func addBoatLayer(to style: MLNStyle) {
            style.setImage(Self.markerImage(systemName: "location.north.fill", color: .systemYellow),
                           forName: Self.boatIconName)
            let source = MLNShapeSource(identifier: Self.boatSourceID, shape: nil, options: nil)
            style.addSource(source)

            let layer = MLNSymbolStyleLayer(identifier: "boat-sym", source: source)
            layer.iconImageName = NSExpression(forConstantValue: Self.boatIconName)
            layer.iconRotation = NSExpression(forKeyPath: "heading")
            layer.iconRotationAlignment = NSExpression(forConstantValue: "map")
            layer.iconAllowsOverlap = NSExpression(forConstantValue: true)
            style.addLayer(layer)
        }

        private func addTargetLayer(to style: MLNStyle) {
            style.setImage(Self.markerImage(systemName: "mappin.circle.fill", color: .systemRed),
                           forName: Self.targetIconName)
            let source = MLNShapeSource(identifier: Self.targetSourceID, shape: nil, options: nil)
            style.addSource(source)

            let layer = MLNSymbolStyleLayer(identifier: "target-sym", source: source)
            layer.iconImageName = NSExpression(forConstantValue: Self.targetIconName)
            layer.iconAllowsOverlap = NSExpression(forConstantValue: true)
            style.addLayer(layer)
        }

        func updateBoat(_ boat: BoatRenderState, on mapView: MLNMapView, follow: Bool) {
            guard let style = mapView.style,
                  let source = style.source(withIdentifier: Self.boatSourceID) as? MLNShapeSource,
                  let coordinate = boat.coordinate else { return }
            hasBoatFix = true
            let feature = MLNPointFeature()
            feature.coordinate = coordinate
            feature.attributes = ["heading": boat.headingDegrees]
            source.shape = feature

            if follow, Date().timeIntervalSince(lastCameraMove) > 1.0 {
                lastCameraMove = Date()
                mapView.setCenter(coordinate, animated: true)
            }
        }

        func updateTarget(_ target: CLLocationCoordinate2D?,
                          boat: CLLocationCoordinate2D?, on mapView: MLNMapView) {
            guard let style = mapView.style,
                  let targetSource = style.source(withIdentifier: Self.targetSourceID) as? MLNShapeSource,
                  let routeSource = style.source(withIdentifier: Self.routeSourceID) as? MLNShapeSource else {
                return
            }
            guard let target else {
                targetSource.shape = nil
                routeSource.shape = nil
                return
            }
            let pin = MLNPointFeature()
            pin.coordinate = target
            targetSource.shape = pin

            if let boat {
                var coords = [boat, target]
                routeSource.shape = MLNPolylineFeature(coordinates: &coords, count: 2)
            } else {
                routeSource.shape = nil
            }
        }

        private static func markerImage(systemName: String, color: UIColor) -> UIImage {
            let config = UIImage.SymbolConfiguration(pointSize: 30, weight: .bold)
            let base = UIImage(systemName: systemName, withConfiguration: config) ?? UIImage()
            return base.withTintColor(color, renderingMode: .alwaysOriginal)
        }
    }
}
