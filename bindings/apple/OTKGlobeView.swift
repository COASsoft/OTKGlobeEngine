import MetalKit
import Network
import OTKGlobeEngineC

#if canImport(UIKit)
import UIKit
public typealias OTEPlatformView = MTKView
#else
import AppKit
public typealias OTEPlatformView = MTKView
#endif

public final class OTKGlobeView: OTEPlatformView, MTKViewDelegate {
    public private(set) var engine: OpaquePointer?
    public var onTap: ((CGPoint, Int) -> Void)?
    public var onLongPress: ((CGPoint) -> Void)?
    private let pathMonitor = NWPathMonitor()

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
            engine = otk_create_metal(Unmanaged.passUnretained(device).toOpaque(), 1)
        }
        if let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first {
            var dir = support.appendingPathComponent("OTKGlobeEngine", isDirectory: true)
            try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            var values = URLResourceValues()
            values.isExcludedFromBackup = true
            try? dir.setResourceValues(values)
            otk_set_cache_directory(engine, dir.path)
        }
        pathMonitor.pathUpdateHandler = { [weak self] path in
            guard path.status == .satisfied else { return }
            DispatchQueue.main.async {
                guard let self = self, let engine = self.engine else { return }
                otk_network_changed(engine)
            }
        }
        pathMonitor.start(queue: DispatchQueue.global(qos: .utility))
        let me = Unmanaged.passUnretained(self).toOpaque()
        otk_set_tap_callback(engine, { user, x, y, taps, _ in
            guard let user = user else { return }
            let view = Unmanaged<OTKGlobeView>.fromOpaque(user).takeUnretainedValue()
            let s = CGFloat(view.scale)
            view.onTap?(CGPoint(x: x / s, y: y / s), Int(taps))
        }, me)
        otk_set_long_press_callback(engine, { user, x, y, _ in
            guard let user = user else { return }
            let view = Unmanaged<OTKGlobeView>.fromOpaque(user).takeUnretainedValue()
            let s = CGFloat(view.scale)
            view.onLongPress?(CGPoint(x: x / s, y: y / s))
        }, me)
        installInput()
    }

    required init(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    deinit {
        pathMonitor.cancel()
        otk_set_tap_callback(engine, nil, nil)
        otk_set_long_press_callback(engine, nil, nil)
        otk_destroy(engine)
    }

    fileprivate var scale: Float {
        #if canImport(UIKit)
        return Float(contentScaleFactor)
        #else
        return Float(window?.backingScaleFactor ?? 1)
        #endif
    }

    public func setView(lat: Double, lon: Double, gsd: Double, azimuth: Double = 0, tilt: Double = 0) {
        var v = otk_view(lat: lat, lon: lon, gsd: gsd, azimuth: azimuth, tilt: tilt)
        otk_set_view(engine, &v)
    }

    public func flyTo(lat: Double, lon: Double, gsd: Double, azimuth: Double = 0, tilt: Double = 0, seconds: Double = 2) {
        var v = otk_view(lat: lat, lon: lon, gsd: gsd, azimuth: azimuth, tilt: tilt)
        otk_fly_to(engine, &v, seconds)
    }

    public func setHeadingLockedPan(_ enabled: Bool) { otk_set_heading_locked_pan(engine, enabled ? 1 : 0) }

    @discardableResult
    public func loadFeatures(path: String) -> Int32 { otk_load_features(engine, path) }

    @discardableResult
    public func addTileset(path: String, maxScreenSpaceError: Double = 16) -> UInt64 { otk_add_tileset(engine, path, maxScreenSpaceError) }

    @discardableResult
    public func addTileset(path: String, maxScreenSpaceError: Double = 16, headers: [String: String], lighting: Int32 = 0, cacheOffline: Bool = true) -> UInt64 {
        let text = headers.map { "\($0.key): \($0.value)" }.joined(separator: "\n")
        return otk_add_tileset_ex(engine, path, maxScreenSpaceError, text, lighting, cacheOffline ? 1 : 0)
    }

    public func credits() -> [String] {
        let n = otk_credits(engine, nil, 0)
        var buffer = [CChar](repeating: 0, count: Int(n) + 1)
        otk_credits(engine, &buffer, n + 1)
        let text = String(cString: buffer)
        return text.isEmpty ? [] : text.components(separatedBy: "\n")
    }

    public func setSunLighting(_ enabled: Bool, date: Date? = nil) {
        otk_set_sun_lighting(engine, enabled ? 1 : 0, date?.timeIntervalSince1970 ?? 0)
    }

    public static func sunMoon(at date: Date, lat: Double, lon: Double) -> otk_sun_moon {
        var r = otk_sun_moon()
        otk_sun_moon_at(date.timeIntervalSince1970, lat, lon, &r)
        return r
    }

    public static func sunMoonTimes(from date: Date, lat: Double, lon: Double, hours: Double = 24) -> otk_twilight {
        var r = otk_twilight()
        otk_sun_moon_times(date.timeIntervalSince1970, lat, lon, hours, &r)
        return r
    }

    public func setOffline(_ offline: Bool) { otk_set_offline(engine, offline ? 1 : 0) }
    public func setCacheLimit(megabytes: Int32) { otk_set_cache_limit(engine, megabytes) }
    public func downloadRegion(layer: Int32 = 0, west: Double, south: Double, east: Double, north: Double, minZoom: Int32, maxZoom: Int32) -> Int32 {
        otk_download_region(engine, layer, west, south, east, north, minZoom, maxZoom)
    }
    public func downloadStatus(_ id: Int32) -> otk_download_status {
        var s = otk_download_status()
        otk_get_download_status(engine, id, &s)
        return s
    }
    public func cancelDownload(_ id: Int32) { otk_cancel_download(engine, id) }
    public func exportTiles(layer: Int32 = 0, west: Double, south: Double, east: Double, north: Double, minZoom: Int32, maxZoom: Int32, path: String) -> Int64 {
        otk_export_tiles(engine, layer, west, south, east, north, minZoom, maxZoom, path)
    }
    public func importTiles(layer: Int32 = 0, path: String) -> Int64 { otk_import_tiles(engine, layer, path) }

    @discardableResult
    public func addSymbol(lat: Double, lon: Double, code: String, name: String = "", altitude: Double = 0) -> UInt64 {
        otk_add_symbol(engine, lat, lon, altitude, code, name)
    }
    @discardableResult
    public func addBullseye(lat: Double, lon: Double, radius: Double, rings: Int32 = 3, bearingStep: Double = 30,
                            reference: otk_north_reference = OTK_NORTH_MAGNETIC, unit: otk_angle_unit = OTK_ANGLE_DEGREES) -> Int32 {
        otk_add_bullseye(engine, lat, lon, radius, rings, bearingStep, reference, unit, 0)
    }
    @discardableResult
    public func addRangeRings(lat: Double, lon: Double, spacing: Double, count: Int32, radials: Int32 = 0) -> Int32 {
        otk_add_range_rings(engine, lat, lon, spacing, count, radials, 0)
    }
    @discardableResult
    public func addRangeBearing(from: (Double, Double), to: (Double, Double), reference: otk_north_reference = OTK_NORTH_MAGNETIC,
                                unit: otk_angle_unit = OTK_ANGLE_DEGREES) -> Int32 {
        otk_add_range_bearing(engine, from.0, from.1, to.0, to.1, reference, unit, OTK_DISTANCE_METRIC, 0)
    }
    @discardableResult
    public func addContoursInView(interval: Double, majorEvery: Int32 = 5) -> Int32 { otk_add_contours_in_view(engine, interval, majorEvery, 0) }
    public func setContours(_ enabled: Bool, interval: Double = 0, unit: otk_distance_unit = OTK_DISTANCE_METRIC) {
        otk_set_contours(engine, enabled ? 1 : 0, interval, unit)
    }
    public var contours: Bool { otk_contours(engine) != 0 }
    public func setTerrainSlope(_ enabled: Bool) { otk_set_terrain_slope(engine, enabled ? 1 : 0, 0.6, 45) }
    public func setGridType(_ type: otk_grid_type) { otk_set_grid_type(engine, type) }
    public var labels: Bool {
        get { otk_labels(engine) != 0 }
        set { otk_set_labels(engine, newValue ? 1 : 0) }
    }
    public func setLabelsSource(_ urlOrPath: String) { otk_set_labels_source(engine, urlOrPath) }
    public var labelsLayer: Int32 { otk_labels_layer(engine) }
    public var controls: UInt32 {
        get { otk_controls(engine) }
        set { otk_set_controls(engine, newValue) }
    }
    public var basemap: Int32 {
        get { otk_basemap(engine) }
        set { otk_set_basemap(engine, newValue) }
    }
    public var basemapNames: [String] {
        (0..<otk_basemap_count(engine)).map { i in
            var buffer = [CChar](repeating: 0, count: 256)
            otk_basemap_name(engine, i, &buffer, Int32(buffer.count))
            return String(cString: buffer)
        }
    }
    @discardableResult
    public func downloadLabels(west: Double, south: Double, east: Double, north: Double, maxZoom: Int32 = 14) -> Int32 {
        otk_download_labels(engine, west, south, east, north, maxZoom)
    }
    public static func formatCoordinate(lat: Double, lon: Double, format: otk_coordinate_format, precision: Int32 = -1) -> String {
        var buffer = [CChar](repeating: 0, count: 128)
        otk_format_coordinate(lat, lon, format, precision, &buffer, Int32(buffer.count))
        return String(cString: buffer)
    }
    public static func parseCoordinate(_ text: String) -> (Double, Double)? {
        var lat = 0.0, lon = 0.0
        return otk_parse_coordinate(text, &lat, &lon) != 0 ? (lat, lon) : nil
    }

    public func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    public func draw(in view: MTKView) {
        guard let drawable = view.currentDrawable, let depth = view.depthStencilTexture else { return }
        otk_frame_metal(engine, Int32(view.drawableSize.width), Int32(view.drawableSize.height),
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

    private static func keyCode(_ k: Int) -> Int32? {
        switch k {
        case 0: return Int32(OTK_KEY_LEFT.rawValue)
        case 1: return Int32(OTK_KEY_RIGHT.rawValue)
        case 2: return Int32(OTK_KEY_UP.rawValue)
        case 3: return Int32(OTK_KEY_DOWN.rawValue)
        case 4: return Int32(OTK_KEY_ZOOM_IN.rawValue)
        case 5: return Int32(OTK_KEY_ZOOM_OUT.rawValue)
        case 6: return Int32(OTK_KEY_ROTATE_LEFT.rawValue)
        case 7: return Int32(OTK_KEY_ROTATE_RIGHT.rawValue)
        case 8: return Int32(OTK_KEY_TILT_UP.rawValue)
        case 9: return Int32(OTK_KEY_TILT_DOWN.rawValue)
        case 10: return Int32(OTK_KEY_RESET_NORTH.rawValue)
        case 11: return Int32(OTK_KEY_RESET_TILT.rawValue)
        default: return nil
        }
    }

    #if canImport(UIKit)
    private var touchIds: [ObjectIdentifier: Int32] = [:]
    private var nextTouchId: Int32 = 1

    private func installInput() {
        isMultipleTouchEnabled = true
        let indirect = [NSNumber(value: UITouch.TouchType.indirectPointer.rawValue)]
        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(onTrackpadPinch(_:)))
        pinch.allowedTouchTypes = indirect
        let rotate = UIRotationGestureRecognizer(target: self, action: #selector(onTrackpadRotate(_:)))
        rotate.allowedTouchTypes = indirect
        let scroll = UIPanGestureRecognizer(target: self, action: #selector(onScroll(_:)))
        scroll.allowedScrollTypesMask = .all
        scroll.allowedTouchTypes = []
        [pinch, rotate, scroll].forEach { addGestureRecognizer($0) }
    }

    public override var canBecomeFirstResponder: Bool { true }

    public override func didMoveToWindow() {
        super.didMoveToWindow()
        if window != nil { becomeFirstResponder() }
    }

    private func modifiers(_ flags: UIKeyModifierFlags) -> Int32 {
        var m: Int32 = 0
        if flags.contains(.shift) { m |= Int32(OTK_MOD_SHIFT.rawValue) }
        if flags.contains(.control) { m |= Int32(OTK_MOD_CTRL.rawValue) }
        if flags.contains(.alternate) { m |= Int32(OTK_MOD_ALT.rawValue) }
        if flags.contains(.command) { m |= Int32(OTK_MOD_SUPER.rawValue) }
        return m
    }

    private func pointerType(_ t: UITouch) -> Int32 {
        switch t.type {
        case .pencil: return Int32(OTK_POINTER_PEN.rawValue)
        case .indirectPointer: return Int32(OTK_POINTER_MOUSE.rawValue)
        default: return Int32(OTK_POINTER_TOUCH.rawValue)
        }
    }

    private func button(_ event: UIEvent?) -> Int32 {
        guard let mask = event?.buttonMask else { return Int32(OTK_BUTTON_LEFT.rawValue) }
        if mask.contains(.secondary) { return Int32(OTK_BUTTON_RIGHT.rawValue) }
        if mask.contains(UIEvent.ButtonMask.button(3)) { return Int32(OTK_BUTTON_MIDDLE.rawValue) }
        return Int32(OTK_BUTTON_LEFT.rawValue)
    }

    public override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        let mods = modifiers(event?.modifierFlags ?? [])
        for t in touches {
            let id = nextTouchId
            nextTouchId += 1
            touchIds[ObjectIdentifier(t)] = id
            let p = px(t.location(in: self))
            otk_pointer_down(engine, id, p.x, p.y, pointerType(t), button(event), mods)
        }
    }

    public override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        let mods = modifiers(event?.modifierFlags ?? [])
        for t in touches {
            guard let id = touchIds[ObjectIdentifier(t)] else { continue }
            let p = px(t.location(in: self))
            otk_pointer_move(engine, id, p.x, p.y, mods)
        }
    }

    public override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        for t in touches {
            guard let id = touchIds.removeValue(forKey: ObjectIdentifier(t)) else { continue }
            let p = px(t.location(in: self))
            otk_pointer_up(engine, id, p.x, p.y)
        }
    }

    public override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        for t in touches {
            guard let id = touchIds.removeValue(forKey: ObjectIdentifier(t)) else { continue }
            otk_pointer_cancel(engine, id)
        }
    }

    @objc private func onTrackpadPinch(_ g: UIPinchGestureRecognizer) {
        let p = px(g.location(in: self))
        otk_magnify(engine, p.x, p.y, Double(g.scale))
        g.scale = 1
    }

    @objc private func onTrackpadRotate(_ g: UIRotationGestureRecognizer) {
        let p = px(g.location(in: self))
        otk_rotate_gesture(engine, p.x, p.y, -Double(g.rotation) * 180 / .pi)
        g.rotation = 0
    }

    @objc private func onScroll(_ g: UIPanGestureRecognizer) {
        let p = px(g.location(in: self))
        let t = g.translation(in: self)
        let s = CGFloat(scale)
        otk_wheel(engine, p.x, p.y, t.x * s, t.y * s, 1, 0)
        g.setTranslation(.zero, in: self)
    }

    private func keyIndex(_ press: UIPress) -> Int? {
        guard let key = press.key else { return nil }
        switch key.keyCode {
        case .keyboardLeftArrow, .keyboardA: return 0
        case .keyboardRightArrow, .keyboardD: return 1
        case .keyboardUpArrow, .keyboardW: return 2
        case .keyboardDownArrow, .keyboardS: return 3
        case .keyboardEqualSign, .keypadPlus: return 4
        case .keyboardHyphen, .keypadHyphen: return 5
        case .keyboardQ: return 6
        case .keyboardE: return 7
        case .keyboardPageUp: return 8
        case .keyboardPageDown: return 9
        case .keyboardN: return 10
        case .keyboardT: return 11
        default: return nil
        }
    }

    public override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var handled = false
        for p in presses {
            if let i = keyIndex(p), let k = OTKGlobeView.keyCode(i) {
                otk_key(engine, k, 1, modifiers(p.key?.modifierFlags ?? []))
                handled = true
            }
        }
        if !handled { super.pressesBegan(presses, with: event) }
    }

    public override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var handled = false
        for p in presses {
            if let i = keyIndex(p), let k = OTKGlobeView.keyCode(i) {
                otk_key(engine, k, 0, 0)
                handled = true
            }
        }
        if !handled { super.pressesEnded(presses, with: event) }
    }

    public override func pressesCancelled(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        for p in presses {
            if let i = keyIndex(p), let k = OTKGlobeView.keyCode(i) { otk_key(engine, k, 0, 0) }
        }
        super.pressesCancelled(presses, with: event)
    }
    #else
    private var mouseButton: Int32 = 0

    private func installInput() {}

    public override var acceptsFirstResponder: Bool { true }

    private func modifiers(_ flags: NSEvent.ModifierFlags) -> Int32 {
        var m: Int32 = 0
        if flags.contains(.shift) { m |= Int32(OTK_MOD_SHIFT.rawValue) }
        if flags.contains(.control) { m |= Int32(OTK_MOD_CTRL.rawValue) }
        if flags.contains(.option) { m |= Int32(OTK_MOD_ALT.rawValue) }
        if flags.contains(.command) { m |= Int32(OTK_MOD_SUPER.rawValue) }
        return m
    }

    private func location(_ event: NSEvent) -> CGPoint { px(convert(event.locationInWindow, from: nil)) }

    private func down(_ event: NSEvent, _ button: Int32) {
        window?.makeFirstResponder(self)
        guard mouseButton == 0 else { return }
        mouseButton = button
        let p = location(event)
        otk_pointer_down(engine, 0, p.x, p.y, Int32(OTK_POINTER_MOUSE.rawValue), button, modifiers(event.modifierFlags))
    }

    private func dragged(_ event: NSEvent) {
        let p = location(event)
        otk_pointer_move(engine, 0, p.x, p.y, modifiers(event.modifierFlags))
    }

    private func up(_ event: NSEvent, _ button: Int32) {
        guard mouseButton == button else { return }
        mouseButton = 0
        let p = location(event)
        otk_pointer_up(engine, 0, p.x, p.y)
    }

    public override func mouseDown(with event: NSEvent) { down(event, Int32(OTK_BUTTON_LEFT.rawValue)) }
    public override func mouseDragged(with event: NSEvent) { dragged(event) }
    public override func mouseUp(with event: NSEvent) { up(event, Int32(OTK_BUTTON_LEFT.rawValue)) }
    public override func rightMouseDown(with event: NSEvent) { down(event, Int32(OTK_BUTTON_RIGHT.rawValue)) }
    public override func rightMouseDragged(with event: NSEvent) { dragged(event) }
    public override func rightMouseUp(with event: NSEvent) { up(event, Int32(OTK_BUTTON_RIGHT.rawValue)) }
    public override func otherMouseDown(with event: NSEvent) { down(event, Int32(OTK_BUTTON_MIDDLE.rawValue)) }
    public override func otherMouseDragged(with event: NSEvent) { dragged(event) }
    public override func otherMouseUp(with event: NSEvent) { up(event, Int32(OTK_BUTTON_MIDDLE.rawValue)) }

    public override func scrollWheel(with event: NSEvent) {
        let p = location(event)
        let s = CGFloat(scale)
        if event.hasPreciseScrollingDeltas {
            otk_wheel(engine, p.x, p.y, event.scrollingDeltaX * s, event.scrollingDeltaY * s, 1, modifiers(event.modifierFlags))
        } else {
            otk_wheel(engine, p.x, p.y, event.scrollingDeltaX, event.scrollingDeltaY, 0, modifiers(event.modifierFlags))
        }
    }

    public override func magnify(with event: NSEvent) {
        let p = location(event)
        otk_magnify(engine, p.x, p.y, 1.0 + Double(event.magnification))
    }

    public override func rotate(with event: NSEvent) {
        let p = location(event)
        otk_rotate_gesture(engine, p.x, p.y, Double(event.rotation))
    }

    private func keyIndex(_ code: UInt16) -> Int? {
        switch code {
        case 123, 0: return 0
        case 124, 2: return 1
        case 126, 13: return 2
        case 125, 1: return 3
        case 24, 69: return 4
        case 27, 78: return 5
        case 12: return 6
        case 14: return 7
        case 116: return 8
        case 121: return 9
        case 45: return 10
        case 17: return 11
        default: return nil
        }
    }

    public override func keyDown(with event: NSEvent) {
        guard let i = keyIndex(event.keyCode), let k = OTKGlobeView.keyCode(i) else {
            super.keyDown(with: event)
            return
        }
        if !event.isARepeat { otk_key(engine, k, 1, modifiers(event.modifierFlags)) }
    }

    public override func keyUp(with event: NSEvent) {
        guard let i = keyIndex(event.keyCode), let k = OTKGlobeView.keyCode(i) else {
            super.keyUp(with: event)
            return
        }
        otk_key(engine, k, 0, 0)
    }
    #endif
}
