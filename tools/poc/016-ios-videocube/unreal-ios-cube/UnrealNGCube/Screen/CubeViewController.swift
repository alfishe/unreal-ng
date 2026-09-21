import UIKit
import Metal
import QuartzCore
import simd

struct CubeVertex {
    var px: Float, py: Float, pz: Float
    var nx: Float, ny: Float, nz: Float
    var u: Float, v: Float
    var faceIndex: UInt32
}

struct CubeUniforms {
    var mvpMatrix: matrix_float4x4 = matrix_identity_float4x4
    var modelMatrix: matrix_float4x4 = matrix_identity_float4x4
    var lightDir: SIMD4<Float> = SIMD4<Float>(0.5, 0.8, 1.0, 0.0)
    var crtWeight: Float = 0.2
    var padding: SIMD3<Float> = SIMD3<Float>(0, 0, 0)
}

final class CubeViewController: UIViewController {

    // Metal Pipeline
    private var metalDevice: MTLDevice!
    private var commandQueue: MTLCommandQueue!
    private var pipelineState: MTLRenderPipelineState!
    private var depthState: MTLDepthStencilState!
    private var depthTexture: MTLTexture?
    private var metalLayer: CAMetalLayer!
    private var displayLink: CADisplayLink?

    private var vertexBuffer: MTLBuffer!
    private var uniformsBuffer: MTLBuffer!

    // 6 Live Textures for 6 Cube Faces
    private var faceTextures: [MTLTexture?] = Array(repeating: nil, count: 6)
    private var faceWidths: [UInt16] = Array(repeating: 320, count: 6)
    private var faceHeights: [UInt16] = Array(repeating: 240, count: 6)

    // Tear-free frame pipeline: the bridge stages complete end-of-frame
    // snapshots (VIDEO_FRAME_REFRESH, posted at the emulator's frame
    // boundary). Each display tick copies the newest snapshot into a slot of
    // a per-face staging ring; a blit encoder then moves it into the private
    // texture inside the SAME command buffer that draws it. The CPU never
    // writes textures directly - that raced in-flight GPU reads from earlier
    // ticks and showed up on screen as tearing.
    private static let stagingRingDepth = 4             // > max in-flight command buffers (drawable count 3)
    private static let stagingCapacity = 384 * 304 * 4  // largest framebuffer (overscan), RGBA8
    private var faceStagingRings: [[MTLBuffer?]] = Array(repeating: Array(repeating: nil, count: 4), count: 6)
    private var faceRingCursor = Array(repeating: 0, count: 6)
    private var faceLastStagedSlot = Array(repeating: -1, count: 6)
    private var faceUploadedSeq = Array(repeating: UInt64(0), count: 6)
    private var faceDirty = Array(repeating: false, count: 6)

    // 3D Physics Engine & Rotation
    private var rotX: Float = 0.3
    private var rotY: Float = 0.6
    private var rotZ: Float = 0.0

    private var velX: Float = 0.005
    private var velY: Float = 0.008
    private var velZ: Float = 0.003

    private var cameraZoomScale: CGFloat = 1.0
    private var initialPinchZoom: CGFloat = 1.0
    private var isAutoRotating: Bool = true
    private var initialTwoFingerRotZ: Float = 0.0   // rotZ at two-finger gesture start

    // HUD
    private let hudOverlayView = UIView()
    private let statusLabel = UILabel()
    private let ipLabel = UILabel()
    private let helpLabel = UILabel()

    var isHUDEnabled: Bool = true {
        didSet { hudOverlayView.isHidden = !isHUDEnabled }
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .black

        setupMetal()
        setupCubeGeometry()
        setupHUD()
        setupGestures()

        UNGCubeBridge.shared().startSixInstances()
        NotificationCenter.default.addObserver(self, selector: #selector(onInstancesChanged), name: NSNotification.Name.UNGCubeInstancesChanged, object: nil)
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
        let scale = view.window?.windowScene?.screen.scale ?? UIScreen.main.scale
        metalLayer.frame = view.bounds
        metalLayer.contentsScale = scale
        metalLayer.drawableSize = CGSize(width: view.bounds.width * scale, height: view.bounds.height * scale)
    }

    @objc private func onInstancesChanged() {
        updateHUDText()
    }

    // MARK: - Metal & 3D Geometry Setup

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
        let scale = UIScreen.main.scale
        metalLayer.contentsScale = scale
        metalLayer.drawableSize = CGSize(width: view.bounds.width * scale, height: view.bounds.height * scale)
        view.layer.insertSublayer(metalLayer, at: 0)

        guard let defaultLibrary = device.makeDefaultLibrary() else {
            print("CubeViewController: Failed to make default Metal library")
            return
        }

        let vertexFunction = defaultLibrary.makeFunction(name: "cubeVertexShader")
        let fragmentFunction = defaultLibrary.makeFunction(name: "cubeFragmentShader")

        let pipelineDescriptor = MTLRenderPipelineDescriptor()
        pipelineDescriptor.vertexFunction = vertexFunction
        pipelineDescriptor.fragmentFunction = fragmentFunction
        pipelineDescriptor.colorAttachments[0].pixelFormat = metalLayer.pixelFormat
        pipelineDescriptor.depthAttachmentPixelFormat = .depth32Float

        do {
            pipelineState = try device.makeRenderPipelineState(descriptor: pipelineDescriptor)
        } catch {
            print("CubeViewController: Failed to create pipeline state: \(error)")
        }

        let depthDescriptor = MTLDepthStencilDescriptor()
        depthDescriptor.depthCompareFunction = .less
        depthDescriptor.isDepthWriteEnabled = true
        depthState = device.makeDepthStencilState(descriptor: depthDescriptor)

        uniformsBuffer = device.makeBuffer(length: MemoryLayout<CubeUniforms>.size, options: .storageModeShared)
    }

    private func setupCubeGeometry() {
        // 6 Faces x 2 Triangles x 3 Vertices = 36 Vertices
        // Face indices: 0:Front, 1:Back, 2:Left, 3:Right, 4:Top, 5:Bottom
        var vertices: [CubeVertex] = []

        let faces: [(normal: SIMD3<Float>, faceIdx: UInt32, quad: [SIMD3<Float>])] = [
            // Front (Face 0)
            (SIMD3(0, 0, 1), 0, [SIMD3(-1, -1, 1), SIMD3(1, -1, 1), SIMD3(1, 1, 1), SIMD3(-1, 1, 1)]),
            // Back (Face 1)
            (SIMD3(0, 0, -1), 1, [SIMD3(1, -1, -1), SIMD3(-1, -1, -1), SIMD3(-1, 1, -1), SIMD3(1, 1, -1)]),
            // Left (Face 2)
            (SIMD3(-1, 0, 0), 2, [SIMD3(-1, -1, -1), SIMD3(-1, -1, 1), SIMD3(-1, 1, 1), SIMD3(-1, 1, -1)]),
            // Right (Face 3)
            (SIMD3(1, 0, 0), 3, [SIMD3(1, -1, 1), SIMD3(1, -1, -1), SIMD3(1, 1, -1), SIMD3(1, 1, 1)]),
            // Top (Face 4)
            (SIMD3(0, 1, 0), 4, [SIMD3(-1, 1, 1), SIMD3(1, 1, 1), SIMD3(1, 1, -1), SIMD3(-1, 1, -1)]),
            // Bottom (Face 5)
            (SIMD3(0, -1, 0), 5, [SIMD3(-1, -1, -1), SIMD3(1, -1, -1), SIMD3(1, -1, 1), SIMD3(-1, -1, 1)])
        ]

        let uvs: [SIMD2<Float>] = [
            SIMD2(0, 1), SIMD2(1, 1), SIMD2(1, 0),
            SIMD2(0, 1), SIMD2(1, 0), SIMD2(0, 0)
        ]

        for face in faces {
            let q = face.quad
            let quadPositions: [SIMD3<Float>] = [q[0], q[1], q[2], q[0], q[2], q[3]]
            for i in 0..<6 {
                let pos = quadPositions[i]
                let n = face.normal
                let uv = uvs[i]
                let v = CubeVertex(px: pos.x, py: pos.y, pz: pos.z,
                                   nx: n.x, ny: n.y, nz: n.z,
                                   u: uv.x, v: uv.y,
                                   faceIndex: face.faceIdx)
                vertices.append(v)
            }
        }

        vertexBuffer = metalDevice.makeBuffer(bytes: vertices, length: vertices.count * MemoryLayout<CubeVertex>.size, options: [])
    }

    private func ensureFaceTexture(index: Int, width: Int, height: Int) {
        if let tex = faceTextures[index], tex.width == width, tex.height == height {
            return
        }

        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba8Unorm,
            width: width,
            height: height,
            mipmapped: false
        )
        descriptor.usage = [.shaderRead]
        // GPU-private: contents arrive exclusively via blit encoders, so no
        // CPU write can ever race an in-flight GPU read (the tearing source)
        descriptor.storageMode = .private
        faceTextures[index] = metalDevice.makeTexture(descriptor: descriptor)
    }

    private func ensureStagingSlot(face: Int, slot: Int) -> MTLBuffer? {
        if let buffer = faceStagingRings[face][slot], buffer.length >= CubeViewController.stagingCapacity {
            return buffer
        }
        guard let buffer = metalDevice.makeBuffer(length: CubeViewController.stagingCapacity, options: .storageModeShared) else {
            return nil
        }
        faceStagingRings[face][slot] = buffer
        return buffer
    }

    // MARK: - Display Loop & Physics

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
        let bridge = UNGCubeBridge.shared()

        // 1. Pull the newest end-of-frame staged snapshots into the staging
        // ring (CPU side). The bridge staged them atomically at the
        // emulator's frame boundary, so every copy is a complete frame
        for i in 0..<6 {
            let seqNow = bridge.stagedFrameSequence(forIndex: i)
            if seqNow == 0 || seqNow == faceUploadedSeq[i] { continue }

            let slot = faceRingCursor[i] % CubeViewController.stagingRingDepth
            guard let staging = ensureStagingSlot(face: i, slot: slot) else { continue }

            var w: UInt16 = 0
            var h: UInt16 = 0
            var seq: UInt64 = 0
            if bridge.copyStagedFrame(forIndex: i, toBuffer: staging.contents(), capacity: staging.length, width: &w, height: &h, sequence: &seq),
               seq != faceUploadedSeq[i] {
                faceUploadedSeq[i] = seq
                faceDirty[i] = true
                faceLastStagedSlot[i] = slot
                faceRingCursor[i] += 1

                if w != faceWidths[i] || h != faceHeights[i] {
                    faceWidths[i] = w
                    faceHeights[i] = h
                    ensureFaceTexture(index: i, width: Int(w), height: Int(h))
                }
            }
        }

        // 2. Physics Simulation: update rotation angles
        rotX += velX
        rotY += velY
        rotZ += velZ

        if !isAutoRotating {
            velX *= 0.95
            velY *= 0.95
            velZ *= 0.95
        }

        render()
    }

    private func render() {
        let drawWidth = Int(metalLayer.drawableSize.width)
        let drawHeight = Int(metalLayer.drawableSize.height)
        guard drawWidth > 0 && drawHeight > 0 else { return }

        guard let drawable = metalLayer.nextDrawable() else { return }

        // Create or Reuse Depth Texture matching current drawableSize
        if depthTexture == nil || depthTexture?.width != drawWidth || depthTexture?.height != drawHeight {
            let depthDescriptor = MTLTextureDescriptor.texture2DDescriptor(
                pixelFormat: .depth32Float,
                width: drawWidth,
                height: drawHeight,
                mipmapped: false
            )
            depthDescriptor.usage = [.renderTarget]
            depthDescriptor.storageMode = .private
            depthTexture = metalDevice.makeTexture(descriptor: depthDescriptor)
        }
        guard let depthTex = depthTexture else { return }

        let renderPass = MTLRenderPassDescriptor()
        renderPass.colorAttachments[0].texture = drawable.texture
        renderPass.colorAttachments[0].loadAction = .clear
        renderPass.colorAttachments[0].clearColor = MTLClearColorMake(0.05, 0.05, 0.08, 1.0)
        renderPass.colorAttachments[0].storeAction = .store

        renderPass.depthAttachment.texture = depthTex
        renderPass.depthAttachment.loadAction = .clear
        renderPass.depthAttachment.clearDepth = 1.0
        renderPass.depthAttachment.storeAction = .dontCare

        guard let commandBuffer = commandQueue.makeCommandBuffer() else { return }

        // Atomic GPU-side upload: ring slot -> private texture via a blit
        // encoder, hardware-ordered before the render encoder inside this
        // command buffer. Ring depth exceeds the layer's in-flight command
        // buffer count, so a CPU refill never collides with a pending blit.
        if let blit = commandBuffer.makeBlitCommandEncoder() {
            for i in 0..<6 where faceDirty[i] {
                faceDirty[i] = false
                let slot = faceLastStagedSlot[i]
                guard slot >= 0,
                      let staging = faceStagingRings[i][slot],
                      let tex = faceTextures[i] else { continue }
                let w = Int(faceWidths[i])
                let h = Int(faceHeights[i])
                guard w > 0, h > 0 else { continue }
                blit.copy(from: staging, sourceOffset: 0,
                          sourceBytesPerRow: w * 4,
                          sourceBytesPerImage: w * h * 4,
                          sourceSize: MTLSize(width: w, height: h, depth: 1),
                          to: tex, destinationSlice: 0, destinationLevel: 0,
                          destinationOrigin: MTLOrigin(x: 0, y: 0, z: 0))
            }
            blit.endEncoding()
        }

        guard let renderEncoder = commandBuffer.makeRenderCommandEncoder(descriptor: renderPass) else { return }

        // Calculate Matrices using aspect ratio from exact drawable dimensions
        let aspect = Float(drawWidth) / Float(drawHeight)
        let projection = matrix_perspective_right_hand(fovyRadians: Float.pi / 3.0, aspect: aspect, nearZ: 0.1, farZ: 100.0)

        let dist = Float(3.5 / cameraZoomScale)
        let viewMatrix = matrix_translation(0, 0, -dist)

        let modelRotX = matrix_rotation_x(rotX)
        let modelRotY = matrix_rotation_y(rotY)
        let modelRotZ = matrix_rotation_z(rotZ)
        let modelMatrix = matrix_multiply(modelRotY, matrix_multiply(modelRotX, modelRotZ))

        let mvp = matrix_multiply(projection, matrix_multiply(viewMatrix, modelMatrix))

        var uniforms = CubeUniforms(
            mvpMatrix: mvp,
            modelMatrix: modelMatrix,
            lightDir: SIMD4<Float>(0.5, 0.8, 1.0, 0.0),
            crtWeight: 0.2,
            padding: SIMD3<Float>(0, 0, 0)
        )

        memcpy(uniformsBuffer.contents(), &uniforms, MemoryLayout<CubeUniforms>.size)

        renderEncoder.setRenderPipelineState(pipelineState)
        renderEncoder.setDepthStencilState(depthState)
        renderEncoder.setVertexBuffer(vertexBuffer, offset: 0, index: 0)
        renderEncoder.setVertexBuffer(uniformsBuffer, offset: 0, index: 1)

        for i in 0..<6 {
            if let tex = faceTextures[i] {
                renderEncoder.setFragmentTexture(tex, index: i)
            } else {
                // Fallback texture
                ensureFaceTexture(index: i, width: 320, height: 240)
                renderEncoder.setFragmentTexture(faceTextures[i], index: i)
            }
        }
        renderEncoder.setFragmentBuffer(uniformsBuffer, offset: 0, index: 1)

        renderEncoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 36)
        renderEncoder.endEncoding()

        commandBuffer.present(drawable)
        commandBuffer.commit()
    }

    // MARK: - Gestures & Interactions

    private func setupGestures() {
        let tripleTap = UITapGestureRecognizer(target: self, action: #selector(handleTripleTap))
        tripleTap.numberOfTapsRequired = 3
        view.addGestureRecognizer(tripleTap)

        let doubleTap = UITapGestureRecognizer(target: self, action: #selector(handleDoubleTap))
        doubleTap.numberOfTapsRequired = 2
        doubleTap.require(toFail: tripleTap)
        view.addGestureRecognizer(doubleTap)

        let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        pan.delegate = self
        // One finger spins; two fingers belong to rotate + pinch
        pan.maximumNumberOfTouches = 1
        view.addGestureRecognizer(pan)

        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch(_:)))
        pinch.delegate = self
        view.addGestureRecognizer(pinch)

        let rotation = UIRotationGestureRecognizer(target: self, action: #selector(handleTwoFingerRotation(_:)))
        rotation.delegate = self
        view.addGestureRecognizer(rotation)
    }

    @objc private func handlePan(_ gesture: UIPanGestureRecognizer) {
        let vel = gesture.velocity(in: view)
        isAutoRotating = false
        velY = Float(vel.x) * 0.00005
        velX = Float(vel.y) * 0.00005
    }

    @objc private func handlePinch(_ gesture: UIPinchGestureRecognizer) {
        switch gesture.state {
        case .began:
            initialPinchZoom = cameraZoomScale
        case .changed:
            cameraZoomScale = min(max(initialPinchZoom * gesture.scale, 0.5), 10.0)
        default:
            break
        }
    }

    @objc private func handleTwoFingerRotation(_ gesture: UIRotationGestureRecognizer) {
        // Two-finger twist rolls the cube around the screen (Z) axis. The
        // gesture reports cumulative radians since .began, so rotZ tracks the
        // fingers 1:1; on release the angular velocity carries over as spin
        switch gesture.state {
        case .began:
            isAutoRotating = false
            velX = 0
            velY = 0
            velZ = 0
            initialTwoFingerRotZ = rotZ
        case .changed:
            rotZ = initialTwoFingerRotZ + Float(gesture.rotation)
        case .ended:
            // rad/s -> per-tick delta at the ~60 Hz physics rate, slightly damped
            velZ = Float(gesture.velocity) / 60.0 * 0.9
        default:
            break
        }
    }

    @objc private func handleDoubleTap() {
        // Double-tap ALWAYS resets camera zoom, angles, and velocity to default fit
        cameraZoomScale = 1.0
        isAutoRotating = true
        rotX = 0.3
        rotY = 0.6
        rotZ = 0.0
        velX = 0.005
        velY = 0.008
        velZ = 0.003

        let generator = UIImpactFeedbackGenerator(style: .medium)
        generator.impactOccurred()
    }

    @objc private func handleTripleTap() {
        // Triple-tap turns on/off HUD overlay
        isHUDEnabled.toggle()
        let generator = UIImpactFeedbackGenerator(style: .light)
        generator.impactOccurred()
    }

    // MARK: - HUD Setup

    private func setupHUD() {
        hudOverlayView.translatesAutoresizingMaskIntoConstraints = false
        hudOverlayView.backgroundColor = UIColor(white: 0.1, alpha: 0.75)
        hudOverlayView.layer.cornerRadius = 12
        hudOverlayView.layer.masksToBounds = true
        hudOverlayView.layer.borderWidth = 1
        hudOverlayView.layer.borderColor = UIColor(white: 0.3, alpha: 0.5).cgColor

        let stack = UIStackView()
        stack.axis = .vertical
        stack.spacing = 6
        stack.translatesAutoresizingMaskIntoConstraints = false

        statusLabel.textColor = .systemGreen
        statusLabel.font = .systemFont(ofSize: 14, weight: .bold)

        ipLabel.textColor = .white
        ipLabel.font = .monospacedSystemFont(ofSize: 13, weight: .medium)
        ipLabel.numberOfLines = 0

        helpLabel.textColor = UIColor(white: 0.7, alpha: 1.0)
        helpLabel.font = .systemFont(ofSize: 11, weight: .regular)
        helpLabel.text = "Swipe: Spin | 2-Finger Twist: Roll | Pinch: Zoom | 2xTap: Reset | 3xTap: HUD"

        stack.addArrangedSubview(statusLabel)
        stack.addArrangedSubview(ipLabel)
        stack.addArrangedSubview(helpLabel)

        hudOverlayView.addSubview(stack)
        view.addSubview(hudOverlayView)

        NSLayoutConstraint.activate([
            hudOverlayView.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 16),
            hudOverlayView.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 16),
            hudOverlayView.widthAnchor.constraint(greaterThanOrEqualToConstant: 260),

            stack.topAnchor.constraint(equalTo: hudOverlayView.topAnchor, constant: 12),
            stack.bottomAnchor.constraint(equalTo: hudOverlayView.bottomAnchor, constant: -12),
            stack.leadingAnchor.constraint(equalTo: hudOverlayView.leadingAnchor, constant: 14),
            stack.trailingAnchor.constraint(equalTo: hudOverlayView.trailingAnchor, constant: -14)
        ])
    }

    private func updateHUDText() {
        let bridge = UNGCubeBridge.shared()
        let ip = bridge.localIPAddress ?? "127.0.0.1"
        let webPort = bridge.webApiPort
        let syncModeStr = bridge.isSingleSyncEnabled ? "SingleSync [Face \(bridge.singleSyncMasterIndex)]" : "Dedicated 6 Cores"
        let coreCount = bridge.createdInstanceCount

        statusLabel.text = "🎲 Unreal-NG 3D Video Cube (\(syncModeStr)) — Cores \(coreCount)/6"
        ipLabel.text = "WebAPI: http://\(ip):\(webPort)/"
        if let status = bridge.webApiStatus {
            // Self-check result: "OK - ...", "CONFLICT - ..." or "UNREACHABLE - ..."
            ipLabel.text = "WebAPI: http://\(ip):\(webPort)/ — \(status)"
            ipLabel.textColor = status.hasPrefix("OK") ? .systemGreen : .systemOrange
        } else {
            ipLabel.textColor = .white
        }
    }
}

extension CubeViewController: UIGestureRecognizerDelegate {
    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
        return true
    }
}

// MARK: - Matrix Helpers

func matrix_perspective_right_hand(fovyRadians fovy: Float, aspect: Float, nearZ: Float, farZ: Float) -> matrix_float4x4 {
    let ys = 1 / tanf(fovy * 0.5)
    let xs = ys / aspect
    let zs = farZ / (nearZ - farZ)
    return matrix_float4x4(columns: (
        SIMD4<Float>(xs,  0,          0,  0),
        SIMD4<Float>( 0, ys,          0,  0),
        SIMD4<Float>( 0,  0,         zs, -1),
        SIMD4<Float>( 0,  0, zs * nearZ,  0)
    ))
}

func matrix_translation(_ x: Float, _ y: Float, _ z: Float) -> matrix_float4x4 {
    return matrix_float4x4(columns: (
        SIMD4<Float>(1, 0, 0, 0),
        SIMD4<Float>(0, 1, 0, 0),
        SIMD4<Float>(0, 0, 1, 0),
        SIMD4<Float>(x, y, z, 1)
    ))
}

func matrix_rotation_x(_ angle: Float) -> matrix_float4x4 {
    let c = cosf(angle)
    let s = sinf(angle)
    return matrix_float4x4(columns: (
        SIMD4<Float>(1, 0,  0, 0),
        SIMD4<Float>(0, c,  s, 0),
        SIMD4<Float>(0, -s, c, 0),
        SIMD4<Float>(0, 0,  0, 1)
    ))
}

func matrix_rotation_y(_ angle: Float) -> matrix_float4x4 {
    let c = cosf(angle)
    let s = sinf(angle)
    return matrix_float4x4(columns: (
        SIMD4<Float>(c, 0, -s, 0),
        SIMD4<Float>(0, 1,  0, 0),
        SIMD4<Float>(s, 0,  c, 0),
        SIMD4<Float>(0, 0,  0, 1)
    ))
}

func matrix_rotation_z(_ angle: Float) -> matrix_float4x4 {
    let c = cosf(angle)
    let s = sinf(angle)
    return matrix_float4x4(columns: (
        SIMD4<Float>( c, s, 0, 0),
        SIMD4<Float>(-s, c, 0, 0),
        SIMD4<Float>( 0, 0, 1, 0),
        SIMD4<Float>( 0, 0, 0, 1)
    ))
}
