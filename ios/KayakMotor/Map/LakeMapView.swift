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

/// Mapa offline (Unit 5): bundlowy blank-style + kontur akwenu z `lake.geojson` +
/// marker łodzi (`MLNSymbolStyleLayer` z `iconRotation`) aktualizowany przez
/// podmianę `MLNShapeSource.shape` (bufor GPU, płynne 10 Hz). Zero sieci.
struct LakeMapView: UIViewRepresentable {
    var boat: BoatRenderState
    var followsBoat: Bool = true

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeUIView(context: Context) -> MLNMapView {
        let mapView = MLNMapView(frame: .zero)
        mapView.styleURL = Bundle.main.url(forResource: "blank-style", withExtension: "json")
        mapView.delegate = context.coordinator
        mapView.logoView.isHidden = true
        mapView.allowsRotating = false
        mapView.setCenter(CLLocationCoordinate2D(latitude: 52.02, longitude: 21.02),
                          zoomLevel: 13, animated: false)
        return mapView
    }

    func updateUIView(_ mapView: MLNMapView, context: Context) {
        context.coordinator.updateBoat(boat, on: mapView, follow: followsBoat)
    }

    final class Coordinator: NSObject, MLNMapViewDelegate {
        private static let boatSourceID = "boat-src"
        private static let boatIconName = "boat-icon"
        private var lastCameraMove = Date.distantPast

        func mapView(_ mapView: MLNMapView, didFinishLoading style: MLNStyle) {
            addLakeContour(to: style)
            addBoatLayer(to: style)
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

        private func addBoatLayer(to style: MLNStyle) {
            style.setImage(Self.boatImage(), forName: Self.boatIconName)
            let source = MLNShapeSource(identifier: Self.boatSourceID, shape: nil, options: nil)
            style.addSource(source)

            let layer = MLNSymbolStyleLayer(identifier: "boat-sym", source: source)
            layer.iconImageName = NSExpression(forConstantValue: Self.boatIconName)
            layer.iconRotation = NSExpression(forKeyPath: "heading")
            layer.iconRotationAlignment = NSExpression(forConstantValue: "map")
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

            // Recentrowanie kamery ≤1 Hz (nie 10 Hz) — inaczej kolejka animacji się zapycha.
            if follow, Date().timeIntervalSince(lastCameraMove) > 1.0 {
                lastCameraMove = Date()
                mapView.setCenter(coordinate, animated: true)
            }
        }

        private static func boatImage() -> UIImage {
            let config = UIImage.SymbolConfiguration(pointSize: 30, weight: .bold)
            let base = UIImage(systemName: "location.north.fill", withConfiguration: config)
                ?? UIImage()
            return base.withTintColor(.systemYellow, renderingMode: .alwaysOriginal)
        }
    }
}
