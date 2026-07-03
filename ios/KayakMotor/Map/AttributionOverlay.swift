import SwiftUI

/// Stała atrybucja OSM (ODbL wymaga widocznego kredytu — bundlowy GeoJSON nie
/// wypełnia wbudowanego ⓘ MapLibre, więc pokazujemy własny overlay).
struct AttributionOverlay: View {
    var body: some View {
        Text("© OpenStreetMap contributors")
            .font(.system(size: 10))
            .foregroundStyle(.white)
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(.black.opacity(0.4), in: Capsule())
    }
}
