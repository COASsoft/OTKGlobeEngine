import MetalKit
import OpenTAKEngineC

#if canImport(UIKit)
import UIKit
public typealias OTEPlatformView = MTKView
#else
import AppKit
public typealias OTEPlatformView = MTKView
#endif

public final class OpenTAKView: OTEPlatformView, MTKViewDelegate {
    public private(set) var engine: OpaquePointer?
    private var lastPan: CGPoint = .zero

    public init(frame: CGRect) {
        let device = MTLCreateSystemDefaultDevice()
        super.init(frame: frame, device: device)
        colorPixelFormat = .bgra8Unorm
        depthStencilPixelFormat = .depth32Float_stencil8
        sampleCount = 1
        enableSetNeedsDisplay = false
        isPaused = false
        preferredFramesPerSecond = 60
        delegate = self
        if let device = device {
            engine = ote_create_metal(Unmanaged.passUnretained(device).toOpaque(), 1)
        }
        if let caches = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first {
            ote_set_cache_directory(engine, caches.path)
        }
        installGestures()
    }

    required init(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    deinit {
        ote_destroy(engine)
    }

    private var scale: Float {
        #if canImport(UIKit)
        return Float(contentScaleFactor)
        #else
        return Float(window?.backingScaleFactor ?? 1)
        #endif
    }

    public func setView(lat: Double, lon: Double, gsd: Double, azimuth: Double = 0, tilt: Double = 0) {
        var v = ote_view(lat: lat, lon: lon, gsd: gsd, azimuth: azimuth, tilt: tilt)
        ote_set_view(engine, &v)
    }

    public func flyTo(lat: Double, lon: Double, gsd: Double, azimuth: Double = 0, tilt: Double = 0, seconds: Double = 2) {
        var v = ote_view(lat: lat, lon: lon, gsd: gsd, azimuth: azimuth, tilt: tilt)
        ote_fly_to(engine, &v, seconds)
    }

    @discardableResult
    public func loadFeatures(path: String) -> Int32 { ote_load_features(engine, path) }

    @discardableResult
    public func addTileset(path: String, maxScreenSpaceError: Double = 16) -> UInt64 { ote_add_tileset(engine, path, maxScreenSpaceError) }

    public func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    public func draw(in view: MTKView) {
        guard let drawable = view.currentDrawable, let depth = view.depthStencilTexture else { return }
        ote_frame_metal(engine, Int32(view.drawableSize.width), Int32(view.drawableSize.height),
                        Unmanaged.passUnretained(drawable).toOpaque(),
                        Unmanaged.passUnretained(depth).toOpaque(), nil, scale)
    }

    private func px(_ p: CGPoint) -> CGPoint {
        let s = CGFloat(scale)
        #if canImport(UIKit)
        return CGPoint(x: p.x * s, y: p.y * s)
        #else
        return CGPoint(x: p.x * s, y: (bounds.height - p.y) * s)
        #endif
    }

    #if canImport(UIKit)
    private func installGestures() {
        addGestureRecognizer(UIPanGestureRecognizer(target: self, action: #selector(onPan(_:))))
        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(onPinch(_:)))
        let rotate = UIRotationGestureRecognizer(target: self, action: #selector(onRotate(_:)))
        let tilt = UIPanGestureRecognizer(target: self, action: #selector(onTilt(_:)))
        tilt.minimumNumberOfTouches = 2
        tilt.maximumNumberOfTouches = 2
        [pinch, rotate, tilt].forEach { addGestureRecognizer($0) }
    }

    @objc private func onPan(_ g: UIPanGestureRecognizer) {
        guard g.numberOfTouches <= 1 else { return }
        let p = px(g.location(in: self))
        if g.state == .began { lastPan = p }
        ote_pan(engine, lastPan.x, lastPan.y, p.x, p.y)
        lastPan = p
    }

    @objc private func onPinch(_ g: UIPinchGestureRecognizer) {
        let p = px(g.location(in: self))
        ote_zoom_at(engine, p.x, p.y, 1.0 / Double(g.scale))
        g.scale = 1
    }

    @objc private func onRotate(_ g: UIRotationGestureRecognizer) {
        ote_rotate(engine, Double(g.rotation) * 180 / .pi)
        g.rotation = 0
    }

    @objc private func onTilt(_ g: UIPanGestureRecognizer) {
        let t = g.translation(in: self)
        if abs(t.y) > abs(t.x) { ote_tilt(engine, -Double(t.y) * 0.25) }
        g.setTranslation(.zero, in: self)
    }
    #else
    private func installGestures() {}

    public override func mouseDown(with event: NSEvent) { lastPan = px(convert(event.locationInWindow, from: nil)) }

    public override func mouseDragged(with event: NSEvent) {
        let p = px(convert(event.locationInWindow, from: nil))
        ote_pan(engine, lastPan.x, lastPan.y, p.x, p.y)
        lastPan = p
    }

    public override func rightMouseDragged(with event: NSEvent) {
        ote_rotate(engine, Double(event.deltaX) * 0.25)
        ote_tilt(engine, -Double(event.deltaY) * 0.2)
    }

    public override func scrollWheel(with event: NSEvent) {
        let p = px(convert(event.locationInWindow, from: nil))
        ote_zoom_at(engine, p.x, p.y, pow(0.9, Double(event.scrollingDeltaY) * 0.1))
    }
    #endif
}
