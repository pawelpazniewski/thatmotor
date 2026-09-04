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

/// Mapa offline: bundlowy `.pmtiles` (Protomaps) + blank-style, marker łodzi
/// (`MLNSymbolStyleLayer` z `iconRotation`), pin celu + etykieta dystansu (łódka→
/// cel) pod pinezką + linia łódź→cel. Geofence
/// „to woda?" liczony wprost z warstwy `basemap-water` przy tapnięciu (bez geojson).
/// Aktualizacja markerów przez podmianę `MLNShapeSource.shape` (bufor GPU). Zero sieci.
/// Most między przyciskami SwiftUI a `MLNMapView` do sterowania zoomem. Trzyma
/// słabą referencję do mapy (ustawianą w `makeUIView`), by nie tworzyć retain cycle.
///
/// `isFollowing`: czy kamera ma automatycznie podążać za łódką. Ręczny gest
/// (przeciągnięcie/pinch) na mapie wyłącza podążanie -- inaczej auto-recenter
/// co 1s w `Coordinator.updateBoat` „wyrywał" widok spod ręki, uniemożliwiając
/// swobodne przesuwanie mapy. Wraca po `recenter(boat:)` (przycisk „Wróć do
/// mojej pozycji").
@MainActor @Observable
final class MapCameraController {
    fileprivate weak var mapView: MLNMapView?
    private(set) var isFollowing = true

    func zoomIn() { step(by: 1) }
    func zoomOut() { step(by: -1) }

    /// Wyśrodkuj widok na „Tobie": łódka (fix z telemetrii) jeśli dostępna, inaczej
    /// pozycja telefonu (GPS). Zachowuje bieżący zoom. Nic nie robi bez żadnej pozycji.
    /// Przywraca też auto-podążanie za łódką (wyłączone wcześniejszym gestem).
    func recenter(boat: CLLocationCoordinate2D?) {
        guard let mapView else { return }
        guard let target = boat ?? mapView.userLocation?.location?.coordinate else { return }
        isFollowing = true
        mapView.setCenter(target, animated: true)
    }

    /// Wywoływane przez `Coordinator`, gdy region mapy zmienił się z powodu
    /// gestu użytkownika (patrz `regionWillChangeWith(reason:)`), nie naszego
    /// wywołania API.
    fileprivate func pauseFollowing() {
        isFollowing = false
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
    /// Tap w mapę: (współrzędna, czy punkt leży na wodzie).
    var onTap: (CLLocationCoordinate2D, Bool) -> Void = { _, _ in }

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
        context.coordinator.camera = camera
        context.coordinator.updateBoat(boat, on: mapView, follow: camera.isFollowing)
        context.coordinator.updateTarget(target, boat: boat.coordinate, on: mapView)
    }

    final class Coordinator: NSObject, MLNMapViewDelegate {
        private static let boatSourceID = "boat-src"
        private static let targetSourceID = "target-src"
        private static let routeSourceID = "route-src"
        private static let headingAxisSourceID = "heading-axis-src"
        /// Długość rysowanej osi dzioba — wydłużona (x3 od pierwszej wersji: 40m
        /// było za krótkie, by dostrzec na wodzie).
        private static let headingAxisLengthMetres = 120.0
        private static let boatIconName = "boat-icon"
        private static let targetIconName = "target-icon"
        private static let targetLabelSourceID = "target-label-src"
        private static let targetLabelIconName = "target-label-icon"
        static let waterLayerID = "basemap-water"
        private var lastCameraMove = Date.distantPast
        private var hasBoatFix = false
        private var hasCenteredOnUser = false
        /* Ostatnia łódka/cel i tekst etykiety dystansu — dystans zależy od pozycji
         * łódki (aktualizowanej ~10 Hz), więc odświeżamy etykietę i przy ruchu celu,
         * i przy ruchu łódki; obrazek re-rasteryzujemy tylko gdy tekst się zmieni. */
        private var labelTargetCoord: CLLocationCoordinate2D?
        private var labelBoatCoord: CLLocationCoordinate2D?
        private var lastDistanceText: String?
        private var lastLabelShapeCoord: CLLocationCoordinate2D?
        /// Tap w mapę: (współrzędna, czy punkt leży na wodzie).
        var onTap: (CLLocationCoordinate2D, Bool) -> Void = { _, _ in }
        weak var camera: MapCameraController?

        /// Region mapy zaczął się zmieniać -- jeśli powód NIE zawiera `.programmatic`
        /// (czyli nie jest to nasze własne `setCenter`/`setZoomLevel`), to gest
        /// użytkownika (pan/pinch/rotate) -- wyłącz auto-podążanie, żeby nie fightować
        /// z ręcznym przesuwaniem mapy.
        func mapView(_ mapView: MLNMapView, regionWillChangeWith reason: MLNCameraChangeReason,
                    animated: Bool) {
            guard !reason.contains(.programmatic) else { return }
            // MLNMapViewDelegate callbacks always land on the main thread (UIKit
            // contract), but the protocol conformance itself is nonisolated, so
            // hop explicitly rather than mark this witness @MainActor (which
            // Swift 6 rejects as a mixed-isolation conformance). Capture the
            // (MainActor-isolated, weakly-held) controller directly, not `self`
            // (this NSObject-derived Coordinator isn't Sendable).
            let target = camera
            Task { @MainActor in target?.pauseFollowing() }
        }

        @MainActor @objc func handleTap(_ recognizer: UITapGestureRecognizer) {
            guard let mapView = recognizer.view as? MLNMapView else { return }
            let point = recognizer.location(in: mapView)
            let coordinate = mapView.convert(point, toCoordinateFrom: mapView)
            // Geofence „to woda?" liczony WPROST z wyrenderowanej warstwy wody —
            // te same akweny, które widać (cały bbox: Zegrze, Mazury, każdy zbiornik),
            // bez osobnego poligonu do utrzymania. Zapytanie ekranowe jest poprawne
            // dokładnie tutaj, w punkcie tapnięcia (kafel jest wtedy załadowany).
            let water = mapView.visibleFeatures(at: point,
                                                styleLayerIdentifiers: [Self.waterLayerID])
            onTap(coordinate, !water.isEmpty)
        }

        func mapView(_ mapView: MLNMapView, didFinishLoading style: MLNStyle) {
            addBasemap(to: style)
            addRouteLine(to: style)
            addHeadingAxisLine(to: style)
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
            let water = MLNFillStyleLayer(identifier: Self.waterLayerID, source: source)
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

        /// Krótka, ciągła linia od łódki w kierunku `heading` z telemetrii (oś
        /// dzioba) — do weryfikacji kompasu na oko: gdy dziób celuje w cel/punkt
        /// orientacyjny, ta linia powinna pokrywać się z żółtą linią łódka→cel.
        /// Styl (ciągła, czarna) celowo różny od trasy (przerywana, żółta).
        private func addHeadingAxisLine(to style: MLNStyle) {
            let source = MLNShapeSource(identifier: Self.headingAxisSourceID, shape: nil, options: nil)
            style.addSource(source)
            let line = MLNLineStyleLayer(identifier: "heading-axis-line", source: source)
            line.lineColor = NSExpression(forConstantValue: UIColor.black)
            line.lineWidth = NSExpression(forConstantValue: 2)
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

            // Etykieta dystansu — osobny symbol tuż pod pinezką. Tekst renderujemy do
            // obrazka (brak glyphs/fontów w bundlu), więc MapLibre pozycjonuje ją sam
            // przy przesuwaniu mapy. Zakotwiczona górą + offset w dół, by minąć pinezkę.
            style.setImage(Self.distanceLabelImage("—"), forName: Self.targetLabelIconName)
            let labelSource = MLNShapeSource(identifier: Self.targetLabelSourceID, shape: nil, options: nil)
            style.addSource(labelSource)
            let labelLayer = MLNSymbolStyleLayer(identifier: "target-label-sym", source: labelSource)
            labelLayer.iconImageName = NSExpression(forConstantValue: Self.targetLabelIconName)
            labelLayer.iconAllowsOverlap = NSExpression(forConstantValue: true)
            labelLayer.iconAnchor = NSExpression(forConstantValue: "top")
            labelLayer.iconOffset = NSExpression(forConstantValue: NSValue(cgVector: CGVector(dx: 0, dy: 16)))
            style.addLayer(labelLayer)
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

            if let axisSource = style.source(withIdentifier: Self.headingAxisSourceID) as? MLNShapeSource {
                let tip = GeoDistance.destination(fromLat: coordinate.latitude, fromLon: coordinate.longitude,
                                                  bearingDegrees: boat.headingDegrees,
                                                  distanceMetres: Self.headingAxisLengthMetres)
                var coords = [coordinate, CLLocationCoordinate2D(latitude: tip.lat, longitude: tip.lon)]
                axisSource.shape = MLNPolylineFeature(coordinates: &coords, count: 2)
            }

            labelBoatCoord = coordinate
            refreshDistanceLabel(on: mapView)

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
                labelTargetCoord = nil
                refreshDistanceLabel(on: mapView)
                return
            }
            let pin = MLNPointFeature()
            pin.coordinate = target
            targetSource.shape = pin

            if let boat {
                var coords = [boat, target]
                routeSource.shape = MLNPolylineFeature(coordinates: &coords, count: 2)
                labelBoatCoord = boat
            } else {
                routeSource.shape = nil
            }
            labelTargetCoord = target
            refreshDistanceLabel(on: mapView)
        }

        /// Odświeża etykietę dystansu przy pinezce (łódka→cel, haversine). Obrazek
        /// re-rasteryzujemy tylko przy zmianie tekstu; ukrywamy etykietę bez celu
        /// lub bez pozycji łódki (nie da się policzyć). MapLibre pozycjonuje symbol.
        private func refreshDistanceLabel(on mapView: MLNMapView) {
            guard let style = mapView.style,
                  let labelSource = style.source(withIdentifier: Self.targetLabelSourceID) as? MLNShapeSource else {
                return
            }
            guard let target = labelTargetCoord, let boat = labelBoatCoord else {
                labelSource.shape = nil
                lastDistanceText = nil
                lastLabelShapeCoord = nil
                return
            }
            let metres = GeoDistance.metres(fromLat: boat.latitude, fromLon: boat.longitude,
                                            toLat: target.latitude, toLon: target.longitude)
            let text = GeoDistance.shortLabel(metres: metres)
            if text != lastDistanceText {
                style.setImage(Self.distanceLabelImage(text), forName: Self.targetLabelIconName)
                lastDistanceText = text
            }
            if target.latitude != lastLabelShapeCoord?.latitude ||
                target.longitude != lastLabelShapeCoord?.longitude {
                let feature = MLNPointFeature()
                feature.coordinate = target
                labelSource.shape = feature
                lastLabelShapeCoord = target
            }
        }

        /// Renderuje dystans jako pigułkę (biały tekst na ciemnym tle) — brak
        /// wbudowanych fontów, więc tekst rasteryzujemy do obrazka symbolu.
        private static func distanceLabelImage(_ text: String) -> UIImage {
            let font = UIFont.systemFont(ofSize: 13, weight: .bold)
            let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: UIColor.white]
            let textSize = (text as NSString).size(withAttributes: attrs)
            let padH: CGFloat = 9, padV: CGFloat = 4
            let size = CGSize(width: ceil(textSize.width) + padH * 2,
                              height: ceil(textSize.height) + padV * 2)
            let renderer = UIGraphicsImageRenderer(size: size)
            return renderer.image { _ in
                let rect = CGRect(origin: .zero, size: size)
                UIColor(red: 0.05, green: 0.13, blue: 0.21, alpha: 0.9).setFill()
                UIBezierPath(roundedRect: rect, cornerRadius: size.height / 2).fill()
                (text as NSString).draw(at: CGPoint(x: padH, y: padV), withAttributes: attrs)
            }
        }

        private static func markerImage(systemName: String, color: UIColor) -> UIImage {
            let config = UIImage.SymbolConfiguration(pointSize: 30, weight: .bold)
            let base = UIImage(systemName: systemName, withConfiguration: config) ?? UIImage()
            return base.withTintColor(color, renderingMode: .alwaysOriginal)
        }
    }
}
