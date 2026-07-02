import SwiftUI

/// Znak marki: „lot nawigacyjny" (kite skierowany w górę) — ten sam motyw co marker
/// łódki (`location.north`) i ikona aplikacji, dla spójnej tożsamości.
struct NavKite: Shape {
    func path(in rect: CGRect) -> Path {
        let w = rect.width, h = rect.height
        func p(_ fx: CGFloat, _ fy: CGFloat) -> CGPoint {
            CGPoint(x: rect.minX + w * fx, y: rect.minY + h * fy)
        }
        var path = Path()
        path.move(to: p(0.50, 0.00))     // dziób
        path.addLine(to: p(0.92, 0.96))  // prawa lotka
        path.addLine(to: p(0.50, 0.70))  // wcięcie rufy
        path.addLine(to: p(0.08, 0.96))  // lewa lotka
        path.closeSubpath()
        return path
    }
}

/// Kafelkowe logo (jak ikona): gradientowe tło + biały kite + fala.
struct BrandLogo: View {
    var size: CGFloat = 120

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: size * 0.22, style: .continuous)
                .fill(SunlightTheme.brandGradient)

            WaterLine()
                .stroke(SunlightTheme.brand.opacity(0.55), style: StrokeStyle(lineWidth: size * 0.045, lineCap: .round))
                .frame(width: size * 0.66, height: size * 0.12)
                .offset(y: size * 0.30)

            NavKite()
                .fill(.white)
                .frame(width: size * 0.44, height: size * 0.50)
                .offset(y: -size * 0.04)
                .shadow(color: .black.opacity(0.25), radius: size * 0.02, y: size * 0.01)
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: size * 0.22, style: .continuous))
    }
}

/// Pojedyncza fala (sinus) — akcent „woda" pod znakiem.
private struct WaterLine: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        let steps = 24
        for i in 0...steps {
            let x = rect.minX + rect.width * CGFloat(i) / CGFloat(steps)
            let y = rect.midY + sin(CGFloat(i) / CGFloat(steps) * .pi * 2) * rect.height * 0.5
            i == 0 ? path.move(to: CGPoint(x: x, y: y)) : path.addLine(to: CGPoint(x: x, y: y))
        }
        return path
    }
}

#Preview {
    ZStack {
        Color.gray
        BrandLogo(size: 160)
    }
}
