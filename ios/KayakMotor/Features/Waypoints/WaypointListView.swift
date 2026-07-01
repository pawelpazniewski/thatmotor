import SwiftUI
import KayakContract

/// Lista waypointów (R4): zapis realnej pozycji łódki, tap → aktywny cel goto,
/// usuwanie. Zero tras — pojedyncze zapisane cele.
struct WaypointListView: View {
    let waypoints: [Waypoint]
    let canSave: Bool
    let onSave: (String) -> Void
    let onSelect: (Waypoint) -> Void
    let onDelete: (Waypoint) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var newName = ""

    var body: some View {
        NavigationStack {
            List {
                Section("Zapisz bieżącą pozycję łódki") {
                    HStack {
                        TextField("Nazwa punktu", text: $newName)
                        Button("Zapisz") {
                            let name = newName.isEmpty ? "Punkt \(waypoints.count + 1)" : newName
                            onSave(name)
                            newName = ""
                        }
                        .disabled(!canSave)
                    }
                    if !canSave {
                        Text("Brak fixu GPS — nie można zapisać")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                }

                Section("Zapisane punkty") {
                    if waypoints.isEmpty {
                        Text("Brak zapisanych punktów").foregroundStyle(.secondary)
                    }
                    ForEach(waypoints) { waypoint in
                        Button {
                            onSelect(waypoint)
                            dismiss()
                        } label: {
                            VStack(alignment: .leading) {
                                Text(waypoint.name).font(.body)
                                Text(String(format: "%.5f, %.5f",
                                            waypoint.target.latDegrees, waypoint.target.lonDegrees))
                                    .font(.caption.monospaced()).foregroundStyle(.secondary)
                            }
                        }
                    }
                    .onDelete { indexSet in
                        indexSet.map { waypoints[$0] }.forEach(onDelete)
                    }
                }
            }
            .navigationTitle("Waypointy")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Gotowe") { dismiss() }
                }
            }
        }
    }
}
