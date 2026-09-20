import UIKit
import Metal
import QuartzCore

// CRT effect parameters - must match Metal shader struct layout
struct CRTParams {
    var inputSize: SIMD2<Float> = SIMD2(320, 240)
    var outputSize: SIMD2<Float> = SIMD2(1920, 1080)
    var scanlineWeight: Float = 0.3      // Basic profile default
    var curvature: Float = 0.02          // Subtle curve
    var bloomStrength: Float = 0.1       // Soft glow
    var brightness: Float = 1.0
    var contrast: Float = 1.0
    var saturation: Float = 1.0

    static var basic: CRTParams {
        var p = CRTParams()
        p.scanlineWeight = 0.3
        p.curvature = 0.02
        p.bloomStrength = 0.1
        return p
    }

    static var none: CRTParams {
        var p = CRTParams()
        p.scanlineWeight = 0.0
        p.curvature = 0.0
        p.bloomStrength = 0.0
        return p
    }
}

final class ScreenViewController: UIViewController {

    // Metal Pipeline
    private var metalDevice: MTLDevice!
    private var commandQueue: MTLCommandQueue!
    private var pipelineState: MTLRenderPipelineState!
    private var crtPipelineState: MTLRenderPipelineState!
    private var metalLayer: CAMetalLayer!
    private var displayLink: CADisplayLink?

    private var vertexBuffer: MTLBuffer!
    private var texCoordBuffer: MTLBuffer!
    private var crtParamsBuffer: MTLBuffer!
    private var texture: MTLTexture?

    private var frameWidth: UInt16 = 320
    private var frameHeight: UInt16 = 240
    private var lastLatchTs: UInt64 = 0

    // Framebuffer Pixel Storage (RGBA8)
    private var pixelBuffer: [UInt8] = []

    // CRT Effect Settings
    var crtEnabled: Bool = true {
        didSet { updateCRTParams() }
    }
    var crtParams: CRTParams = .basic {
        didSet { updateCRTParams() }
    }

    // HUD Layer
    private let hudOverlayView = UIView()
    private let statusLabel = UILabel()
    private let ipLabel = UILabel()
    private let portsLabel = UILabel()
    private let helpLabel = UILabel()

    var isHUDEnabled: Bool = true {
        didSet {
            hudOverlayView.isHidden = !isHUDEnabled
        }
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .black

        setupMetal()
        setupHUD()
        setupGestures()
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        startDisplayLink()
        updateHUDText()
    }

    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        stopDisplayLink()
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        metalLayer.frame = view.bounds
        metalLayer.contentsScale = UIScreen.main.scale
        updateQuadGeometry()
        updateCRTParams()
    }

    // MARK: - Metal Setup

    private func setupMetal() {
        guard let device = MTLCreateSystemDefaultDevice() else {
            fatalError("Metal is not supported on this device")
        }
        self.metalDevice = device
        self.commandQueue = device.makeCommandQueue()

        metalLayer = CAMetalLayer()
        metalLayer.device = device
        metalLayer.pixelFormat = .bgra8Unorm
        metalLayer.framebufferOnly = true
        metalLayer.frame = view.bounds
        metalLayer.contentsScale = UIScreen.main.scale
        view.layer.addSublayer(metalLayer)

        guard let defaultLibrary = device.makeDefaultLibrary() else {
            print("ScreenViewController: Failed to make default Metal library")
            return
        }

        // Simple passthrough pipeline (CRT disabled)
        let vertexFunction = defaultLibrary.makeFunction(name: "presentVertexShader")
        let fragmentFunction = defaultLibrary.makeFunction(name: "presentFragmentShader")

        let pipelineDescriptor = MTLRenderPipelineDescriptor()
        pipelineDescriptor.vertexFunction = vertexFunction
        pipelineDescriptor.fragmentFunction = fragmentFunction
        pipelineDescriptor.colorAttachments[0].pixelFormat = metalLayer.pixelFormat

        do {
            pipelineState = try device.makeRenderPipelineState(descriptor: pipelineDescriptor)
        } catch {
            print("ScreenViewController: Failed to create pipeline state: \(error)")
        }

        // CRT effect pipeline
        let crtFragmentFunction = defaultLibrary.makeFunction(name: "presentCRTFragmentShader")
        pipelineDescriptor.fragmentFunction = crtFragmentFunction

        do {
            crtPipelineState = try device.makeRenderPipelineState(descriptor: pipelineDescriptor)
        } catch {
            print("ScreenViewController: Failed to create CRT pipeline state: \(error)")
        }

        let positions: [Float] = [
            -1.0, -1.0,
             1.0, -1.0,
            -1.0,  1.0,
             1.0,  1.0
        ]
        let texCoords: [Float] = [
            0.0, 1.0,
            1.0, 1.0,
            0.0, 0.0,
            1.0, 0.0
        ]

        vertexBuffer = device.makeBuffer(bytes: positions, length: positions.count * MemoryLayout<Float>.size, options: [])
        texCoordBuffer = device.makeBuffer(bytes: texCoords, length: texCoords.count * MemoryLayout<Float>.size, options: [])

        // CRT parameters buffer
        crtParamsBuffer = device.makeBuffer(length: MemoryLayout<CRTParams>.size, options: .storageModeShared)
        updateCRTParams()
    }

    private func updateCRTParams() {
        guard let buffer = crtParamsBuffer else { return }

        var params = crtEnabled ? crtParams : .none
        params.inputSize = SIMD2(Float(frameWidth), Float(frameHeight))
        params.outputSize = SIMD2(Float(view.bounds.width * UIScreen.main.scale),
                                   Float(view.bounds.height * UIScreen.main.scale))

        memcpy(buffer.contents(), &params, MemoryLayout<CRTParams>.size)
    }

    private func updateQuadGeometry() {
        guard frameWidth > 0, frameHeight > 0 else { return }

        let bounds = view.bounds
        let aspectView = bounds.width / bounds.height
        let aspectFrame = CGFloat(frameWidth) / CGFloat(frameHeight)

        var scaleX: Float = 1.0
        var scaleY: Float = 1.0

        if aspectView > aspectFrame {
            scaleX = Float(aspectFrame / aspectView)
        } else {
            scaleY = Float(aspectView / aspectFrame)
        }

        let positions: [Float] = [
            -scaleX, -scaleY,
             scaleX, -scaleY,
            -scaleX,  scaleY,
             scaleX,  scaleY
        ]

        vertexBuffer = metalDevice.makeBuffer(bytes: positions, length: positions.count * MemoryLayout<Float>.size, options: [])
    }

    private func ensureTexture(width: Int, height: Int) {
        if texture != nil, texture?.width == width, texture?.height == height {
            return
        }

        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba8Unorm,
            width: width,
            height: height,
            mipmapped: false
        )
        descriptor.usage = [.shaderRead]
        texture = metalDevice.makeTexture(descriptor: descriptor)
        pixelBuffer = [UInt8](repeating: 0, count: width * height * 4)
    }

    // MARK: - CADisplayLink Loop

    private func startDisplayLink() {
        stopDisplayLink()
        displayLink = CADisplayLink(target: self, selector: #selector(onDisplayTick))
        displayLink?.preferredFrameRateRange = CAFrameRateRange(minimum: 50, maximum: 60, preferred: 50)
        displayLink?.add(to: .main, forMode: .common)
    }

    private func stopDisplayLink() {
        displayLink?.invalidate()
        displayLink = nil
    }

    @objc private func onDisplayTick() {
        let bridge = UNGBridge.shared()
        var w: UInt16 = 0
        var h: UInt16 = 0
        var ts: UInt64 = 0

        guard bridge.getFrameWidth(&w, height: &h, latchTimestampUs: &ts) else { return }

        if w != frameWidth || h != frameHeight {
            frameWidth = w
            frameHeight = h
            updateQuadGeometry()
            ensureTexture(width: Int(w), height: Int(h))
            updateCRTParams()
        }

        if ts != lastLatchTs {
            lastLatchTs = ts
            let bufferSize = Int(w) * Int(h) * 4
            if pixelBuffer.count != bufferSize {
                pixelBuffer = [UInt8](repeating: 0, count: bufferSize)
            }
            if bridge.copyFrame(toBuffer: &pixelBuffer, size: bufferSize) {
                let region = MTLRegionMake2D(0, 0, Int(w), Int(h))
                texture?.replace(region: region, mipmapLevel: 0, withBytes: pixelBuffer, bytesPerRow: Int(w) * 4)
            }
        }

        render()
    }

    private func render() {
        guard let drawable = metalLayer.nextDrawable(),
              let texture = texture else { return }

        let activePipeline = crtEnabled ? crtPipelineState : pipelineState
        guard let activePipeline = activePipeline else { return }

        let renderPassDescriptor = MTLRenderPassDescriptor()
        renderPassDescriptor.colorAttachments[0].texture = drawable.texture
        renderPassDescriptor.colorAttachments[0].loadAction = .clear
        renderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1)
        renderPassDescriptor.colorAttachments[0].storeAction = .store

        guard let commandBuffer = commandQueue.makeCommandBuffer(),
              let renderEncoder = commandBuffer.makeRenderCommandEncoder(descriptor: renderPassDescriptor) else { return }

        renderEncoder.setRenderPipelineState(activePipeline)
        renderEncoder.setVertexBuffer(vertexBuffer, offset: 0, index: 0)
        renderEncoder.setVertexBuffer(texCoordBuffer, offset: 0, index: 1)
        renderEncoder.setFragmentTexture(texture, index: 0)

        if crtEnabled {
            renderEncoder.setFragmentBuffer(crtParamsBuffer, offset: 0, index: 0)
        }

        renderEncoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        renderEncoder.endEncoding()

        commandBuffer.present(drawable)
        commandBuffer.commit()
    }

    // MARK: - HUD Layer Setup

    private func setupHUD() {
        hudOverlayView.translatesAutoresizingMaskIntoConstraints = false
        hudOverlayView.backgroundColor = UIColor(white: 0.1, alpha: 0.75)
        hudOverlayView.layer.cornerRadius = 12
        hudOverlayView.layer.masksToBounds = true
        hudOverlayView.layer.borderWidth = 1
        hudOverlayView.layer.borderColor = UIColor(white: 0.3, alpha: 0.5).cgColor

        let stack = UIStackView()
        stack.axis = .vertical
        stack.alignment = .fill
        stack.distribution = .equalSpacing
        stack.spacing = 6
        stack.translatesAutoresizingMaskIntoConstraints = false

        statusLabel.textColor = .systemGreen
        statusLabel.font = .systemFont(ofSize: 14, weight: .bold)

        ipLabel.textColor = .white
        ipLabel.font = .monospacedSystemFont(ofSize: 13, weight: .medium)

        portsLabel.textColor = .lightGray
        portsLabel.font = .monospacedSystemFont(ofSize: 12, weight: .regular)

        helpLabel.textColor = UIColor(white: 0.7, alpha: 1.0)
        helpLabel.font = .systemFont(ofSize: 11, weight: .regular)
        helpLabel.text = "Double-tap: HUD | Triple-tap: CRT"

        stack.addArrangedSubview(statusLabel)
        stack.addArrangedSubview(ipLabel)
        stack.addArrangedSubview(portsLabel)
        stack.addArrangedSubview(helpLabel)

        hudOverlayView.addSubview(stack)
        view.addSubview(hudOverlayView)

        NSLayoutConstraint.activate([
            hudOverlayView.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 16),
            hudOverlayView.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 16),
            hudOverlayView.widthAnchor.constraint(greaterThanOrEqualToConstant: 240),

            stack.topAnchor.constraint(equalTo: hudOverlayView.topAnchor, constant: 12),
            stack.bottomAnchor.constraint(equalTo: hudOverlayView.bottomAnchor, constant: -12),
            stack.leadingAnchor.constraint(equalTo: hudOverlayView.leadingAnchor, constant: 14),
            stack.trailingAnchor.constraint(equalTo: hudOverlayView.trailingAnchor, constant: -14)
        ])
    }

    private func updateHUDText() {
        let bridge = UNGBridge.shared()
        let ip = bridge.localIPAddress ?? "127.0.0.1"
        let webPort = bridge.webApiPort
        let cliPort = bridge.cliPort

        statusLabel.text = "● Unreal-NG Headless Core"
        ipLabel.text = "IP: \(ip)"
        portsLabel.text = "WebAPI: http://\(ip):\(webPort)/\nCLI Port: \(cliPort)"
    }

    private func setupGestures() {
        let doubleTap = UITapGestureRecognizer(target: self, action: #selector(handleDoubleTap))
        doubleTap.numberOfTapsRequired = 2
        view.addGestureRecognizer(doubleTap)

        let tripleTap = UITapGestureRecognizer(target: self, action: #selector(handleTripleTap))
        tripleTap.numberOfTapsRequired = 3
        view.addGestureRecognizer(tripleTap)

        doubleTap.require(toFail: tripleTap)
    }

    @objc private func handleDoubleTap() {
        isHUDEnabled.toggle()
    }

    @objc private func handleTripleTap() {
        crtEnabled.toggle()

        // Brief feedback
        let generator = UIImpactFeedbackGenerator(style: .light)
        generator.impactOccurred()
    }

    // MARK: - Status Bar

    override var prefersStatusBarHidden: Bool {
        return true
    }

    override var prefersHomeIndicatorAutoHidden: Bool {
        return true
    }

    // MARK: - Keyboard Handling (Hardware / Bluetooth / Simulator Keyboard)

    override var canBecomeFirstResponder: Bool {
        return true
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        becomeFirstResponder()
    }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var handled = false
        for press in presses {
            if let keyName = resolveZXKeyName(from: press) {
                UNGBridge.shared().pressKey(keyName)
                handled = true
            }
        }
        if !handled {
            super.pressesBegan(presses, with: event)
        }
    }

    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var handled = false
        for press in presses {
            if let keyName = resolveZXKeyName(from: press) {
                UNGBridge.shared().releaseKey(keyName)
                handled = true
            }
        }
        if !handled {
            super.pressesEnded(presses, with: event)
        }
    }

    override func pressesCancelled(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        for press in presses {
            if let keyName = resolveZXKeyName(from: press) {
                UNGBridge.shared().releaseKey(keyName)
            }
        }
        super.pressesCancelled(presses, with: event)
    }

    private func resolveZXKeyName(from press: UIPress) -> String? {
        guard let key = press.key else { return nil }

        switch key.keyCode {
        case .keyboardUpArrow:
            return "up"
        case .keyboardDownArrow:
            return "down"
        case .keyboardLeftArrow:
            return "left"
        case .keyboardRightArrow:
            return "right"
        case .keyboardReturnOrEnter:
            return "enter"
        case .keyboardSpacebar:
            return "space"
        case .keyboardDeleteOrBackspace:
            return "delete"
        case .keyboardEscape:
            return "break"
        case .keyboardLeftShift, .keyboardRightShift:
            return "caps"
        case .keyboardLeftControl, .keyboardRightControl,
             .keyboardLeftAlt, .keyboardRightAlt,
             .keyboardLeftGUI, .keyboardRightGUI:
            return "symbol"
        default:
            let chars = key.charactersIgnoringModifiers.lowercased()
            if chars.count == 1 {
                let char = chars.first!
                if (char >= "a" && char <= "z") || (char >= "0" && char <= "9") {
                    return String(char)
                }
            }
            return nil
        }
    }
}
