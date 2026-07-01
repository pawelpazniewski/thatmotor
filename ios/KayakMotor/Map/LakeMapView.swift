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
struct LakeMapView: UIViewRepresentable {
    var boat: BoatRenderState
    var target: CLLocationCoordinate2D?
    var followsBoat: Bool = true
    var onTap: (CLLocationCoordinate2D) -> Void = { _ in }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeUIView(context: Context) -> MLNMapView {
        let mapView = MLNMapView(frame: .zero)
        mapView.styleURL = Bundle.main.url(forResource: "blank-style", withExtension: "json")
        mapView.delegate = context.coordinator
        mapView.logoView.isHidden = true
        mapView.allowsRotating = false
        mapView.setCenter(CLLocationCoordinate2D(latitude: 52.02, longitude: 21.02),
                          zoomLevel: 13, animated: false)

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
        var onTap: (CLLocationCoordinate2D) -> Void = { _ in }

        @objc func handleTap(_ recognizer: UITapGestureRecognizer) {
            guard let mapView = recognizer.view as? MLNMapView else { return }
            let point = recognizer.location(in: mapView)
            onTap(mapView.convert(point, toCoordinateFrom: mapView))
        }

        func mapView(_ mapView: MLNMapView, didFinishLoading style: MLNStyle) {
            addLakeContour(to: style)
            addRouteLine(to: style)
            addBoatLayer(to: style)
            addTargetLayer(to: style)
        }

        private func addLakeContour(to style: MLNStyle) {
            guard let url = Bundle.main.url(forResource: "lake", withExtension: "geojson") else { return }
            let source = MLNShapeSource(identifier: "lake-src", url: url, options: nil)
            style.addSource(source)

            let fill = MLNFillStyleLayer(identifier: "lake-fill", source: source)
            fill.fillColor = NSExpression(forConstantValue: UIColor(red: 0.20, green: 0.50, blue: 0.80, alpha: 1))
            fill.fillOpacity = NSExpression(forConstantValue: 0.85)
            style.addLayer(fill)

            let line = MLNLineStyleLayer(identifier: "lake-outline", source: source)
            line.lineColor = NSExpression(forConstantValue: UIColor.white)
            line.lineWidth = NSExpression(forConstantValue: 1.5)
            style.addLayer(line)
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
