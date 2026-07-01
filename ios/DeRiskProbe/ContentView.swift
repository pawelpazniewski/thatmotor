import SwiftUI

struct ContentView: View {
    @State private var model = ProbeModel()

    var body: some View {
        NavigationStack {
            VStack(spacing: 12) {
                Text("Bramka de-risk — łączność z \(MotorAP.ssid)")
                    .font(.headline)
                    .multilineTextAlignment(.center)

                HStack {
                    Text("Ramki WS:")
                    Text("\(model.frameCount)").bold().monospacedDigit()
                }
                .font(.title3)

                VStack(spacing: 8) {
                    Button("1. Dołącz do AP") { Task { await model.joinAP() } }
                    Button("2. HTTP POST goto_cancel") { Task { await model.httpPost() } }
                    Button("3. Start WebSocket") { model.startWebSocket() }
                    Button("Stop WebSocket") { model.stopWebSocket() }
                        .tint(.red)
                }
                .buttonStyle(.borderedProminent)
                .disabled(model.isBusy)

                Divider()

                ScrollView {
                    VStack(alignment: .leading, spacing: 4) {
                        ForEach(Array(model.log.enumerated()), id: \.offset) { _, line in
                            Text(line)
                                .font(.system(.caption, design: .monospaced))
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                    }
                }
            }
            .padding()
            .navigationTitle("DeRisk")
            .navigationBarTitleDisplayMode(.inline)
        }
    }
}

#Preview {
    ContentView()
}
