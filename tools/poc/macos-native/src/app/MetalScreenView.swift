import Combine
import Metal
import MetalKit
import SwiftUI

// MARK: - Shaders
//
// Compiled at runtime (device.makeLibrary(source:)) so the build needs no metallib
// step. The quad is generated from vertex_id; `scale` applies the letterbox /
// pillarbox correction, and the sampler uses nearest filtering for crisp pixels.

private let shaderSource = """
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut screen_vertex(uint vid [[vertex_id]],
                               constant float2& scale [[buffer(0)]])
{
    // Two triangles covering [-1,1]^2
    const float2 corners[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2(-1.0,  1.0),
        float2(-1.0,  1.0), float2( 1.0, -1.0), float2( 1.0,  1.0)
    };

    float2 p = corners[vid];

    VertexOut out;
    out.position = float4(p * scale, 0.0, 1.0);
    // Flip V: the framebuffer's first row is the top row
    out.uv = float2((p.x + 1.0) * 0.5, 1.0 - (p.y + 1.0) * 0.5);
    return out;
}

fragment float4 screen_fragment(VertexOut in [[stage_in]],
                                texture2d<float> tex [[texture(0)]])
{
    constexpr sampler s(filter::nearest, address::clamp_to_edge);
    return tex.sample(s, in.uv);
}
"""

// MARK: - NSView

/// MTKView subclass that owns the texture upload, the letterboxed quad draw and
/// keyboard capture. Redraws are driven by the emulator's frame notification
/// (`isPaused = true` + `enableSetNeedsDisplay = true`), not by a free-running timer.
final class EmulatorMetalView: MTKView, MTKViewDelegate {
    weak var controller: EmulatorController?
    /// Told about the real picture area so it can size the window against it rather
    /// than against an assumed chrome height (BUG 5).
    weak var windowManager: WindowManager?

    private var commandQueue: MTLCommandQueue?
    private var pipeline: MTLRenderPipelineState?
    private var texture: MTLTexture?
    private var textureSize = CGSize.zero
    private var frameSubscription: AnyCancellable?
    private var focusSubscription: AnyCancellable?

    init(controller: EmulatorController) {
        self.controller = controller
        let device = MTLCreateSystemDefaultDevice()
        super.init(frame: .zero, device: device)

        colorPixelFormat = .bgra8Unorm
        clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
        isPaused = true
        enableSetNeedsDisplay = true
        autoResizeDrawable = true
        layer?.isOpaque = true

        buildPipeline()
        delegate = self

        frameSubscription = controller.frameTick
            .receive(on: RunLoop.main)
            .sink { [weak self] in
                guard let self, self.windowManager?.isTransitioning != true else { return }
                self.needsDisplay = true
            }

        // A toolbar button or menu item can take key focus away from the screen; the
        // emulator is useless without it, so take it straight back.
        focusSubscription = controller.focusRequest
            .receive(on: RunLoop.main)
            .sink { [weak self] in
                guard let self, let window = self.window else { return }
                window.makeFirstResponder(self)
            }
    }

    required init(coder: NSCoder) {
        fatalError("init(coder:) is not used")
    }

    private func buildPipeline() {
        guard let device else {
            NSLog("[EmulatorMetalView] No Metal device available")
            return
        }

        commandQueue = device.makeCommandQueue()

        do {
            let library = try device.makeLibrary(source: shaderSource, options: nil)
            let descriptor = MTLRenderPipelineDescriptor()
            descriptor.vertexFunction = library.makeFunction(name: "screen_vertex")
            descriptor.fragmentFunction = library.makeFunction(name: "screen_fragment")
            descriptor.colorAttachments[0].pixelFormat = colorPixelFormat
            pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
        } catch {
            NSLog("[EmulatorMetalView] Failed to build pipeline: \(error)")
        }
    }

    private func ensureTexture(width: Int, height: Int) -> MTLTexture? {
        if let texture, Int(textureSize.width) == width, Int(textureSize.height) == height {
            return texture
        }
        guard let device, width > 0, height > 0 else { return nil }

        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba8Unorm,
            width: width,
            height: height,
            mipmapped: false)
        descriptor.usage = .shaderRead
        descriptor.storageMode = .managed

        texture = device.makeTexture(descriptor: descriptor)
        textureSize = CGSize(width: width, height: height)
        return texture
    }

    // MARK: Drawing

    // MTKViewDelegate - the supported redraw entry point. Overriding MTKView.draw()
    // directly races AppKit's own display pass and can present a drawable twice.
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        // Silent for the whole transition. settleAfterTransition() calls draw()
        // directly and clears the flag first, so the final frame still lands.
        guard forceDraw || windowManager?.isTransitioning != true else { return }

        guard
            let controller,
            let pipeline,
            let commandQueue,
            let descriptor = currentRenderPassDescriptor,
            let drawable = currentDrawable
        else { return }

        // Upload the current framebuffer
        controller.bridge.accessFramebuffer { bytes, width, height, byteSize in
            guard
                let texture = self.ensureTexture(width: Int(width), height: Int(height)),
                byteSize >= Int(width) * Int(height) * 4
            else { return }

            texture.replace(region: MTLRegionMake2D(0, 0, Int(width), Int(height)),
                            mipmapLevel: 0,
                            withBytes: bytes,
                            bytesPerRow: Int(width) * 4)
        }

        guard let commandBuffer = commandQueue.makeCommandBuffer(),
              let encoder = commandBuffer.makeRenderCommandEncoder(descriptor: descriptor)
        else { return }

        if let texture {
            // The quad always covers the whole viewport; the letterbox/pillarbox is
            // expressed as an integer-pixel viewport rect instead of a float NDC
            // scale. Whole-pixel arithmetic is what makes an exact NxN window show
            // zero bars - a float scale of 0.99998 still rounds to a visible seam.
            let viewport = pictureViewport(textureSize: textureSize,
                                           drawableSize: drawableSize)
            encoder.setViewport(viewport)
            encoder.setRenderPipelineState(pipeline)
            var scale = SIMD2<Float>(1, 1)
            encoder.setVertexBytes(&scale, length: MemoryLayout<SIMD2<Float>>.size, index: 0)
            encoder.setFragmentTexture(texture, index: 0)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6)

            logGeometryIfNeeded(viewport: viewport)
        }

        encoder.endEncoding()

        if presentsWithTransaction {
            // Sync path: the present must land in the CATransaction we are inside.
            commandBuffer.commit()
            commandBuffer.waitUntilScheduled()
            drawable.present()
        } else {
            commandBuffer.present(drawable)
            commandBuffer.commit()
        }
    }

    /// Where the emulated picture goes inside the drawable, in whole device pixels.
    ///
    /// At an integer window scale the drawable is exactly framebuffer * scale *
    /// backingScaleFactor, `fit` comes out an exact integer and the viewport is the
    /// whole drawable - no bars at all. Off-integer sizes (a dragged window edge)
    /// letterbox as before, snapped to whole pixels.
    private func pictureViewport(textureSize: CGSize, drawableSize: CGSize) -> MTLViewport {
        guard textureSize.width > 0, textureSize.height > 0,
              drawableSize.width > 0, drawableSize.height > 0
        else {
            return MTLViewport(originX: 0, originY: 0,
                               width: drawableSize.width, height: drawableSize.height,
                               znear: 0, zfar: 1)
        }

        let fit = min(drawableSize.width / textureSize.width,
                      drawableSize.height / textureSize.height)

        var width = (textureSize.width * fit).rounded()
        var height = (textureSize.height * fit).rounded()

        // Absorb sub-pixel rounding: never leave a 1px seam we could have filled.
        if drawableSize.width - width < 2 { width = drawableSize.width }
        if drawableSize.height - height < 2 { height = drawableSize.height }

        return MTLViewport(originX: ((drawableSize.width - width) / 2).rounded(),
                           originY: ((drawableSize.height - height) / 2).rounded(),
                           width: width,
                           height: height,
                           znear: 0,
                           zfar: 1)
    }

    /// One-shot geometry dump, re-armed whenever the framebuffer or the view size
    /// changes. Deliberately not per frame.
    private var lastLoggedGeometry: String = ""

    private func logGeometryIfNeeded(viewport: MTLViewport) {
        guard GeometryLog.enabled else { return }
        let backing = window?.backingScaleFactor ?? 1
        let line = String(format:
            "fb=%.0fx%.0f view=%.1fx%.1f backing=%.1f drawable=%.0fx%.0f viewport=%.0f,%.0f %.0fx%.0f",
            textureSize.width, textureSize.height,
            bounds.width, bounds.height, backing,
            drawableSize.width, drawableSize.height,
            viewport.originX, viewport.originY, viewport.width, viewport.height)

        guard line != lastLoggedGeometry else { return }
        lastLoggedGeometry = line
        NSLog("[EmulatorMetalView] %@", line)
    }

    // MARK: Keyboard

    override func layout() {
        super.layout()
        // Nobody writes drawable geometry during a fullscreen transition.
        guard windowManager?.isTransitioning != true else { return }
        windowManager?.report(pictureSize: bounds.size)
    }

    // MARK: Fullscreen transition

    /// Set only around the deliberate in-transition redraws below, which have to get
    /// past the `isTransitioning` guard that silences everything else.
    private var forceDraw = false

    /// One frame at whatever geometry the view has RIGHT NOW, drawn synchronously.
    ///
    /// The teleport gives the view its fullscreen bounds in an instant, and
    /// autoResizeDrawable then reallocates the drawable - which comes up cleared. With
    /// the frame pump silent nothing would ever fill it, and the zoom would animate a
    /// black rectangle. That was the black flash. Rendering once here means the layer
    /// carries a real picture at the target resolution before the transform shrinks it
    /// back to where the window used to be.
    func renderForTransition() {
        layoutSubtreeIfNeeded()

        let backing = window?.backingScaleFactor ?? 1
        let size = CGSize(width: (bounds.width * backing).rounded(),
                          height: (bounds.height * backing).rounded())

        forceDraw = true
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        if size.width > 0, size.height > 0, size != drawableSize {
            drawableSize = size
        }
        presentsWithTransaction = true
        draw()
        presentsWithTransaction = false
        CATransaction.commit()
        forceDraw = false
    }

    /// Stop PRODUCING frames for the duration of the transition.
    ///
    /// Note what this deliberately does NOT do: it does not clear
    /// `autoResizeDrawable`. That was tried and it is what left the picture stuck at
    /// its windowed size in the top-left corner of the fullscreen window - MTKView
    /// only recomputes `drawableSize` from a bounds change, so a bounds change that
    /// happens while the flag is off is lost forever and no later layout brings it
    /// back. The teleport resizes the window exactly once, so there is no per-step
    /// drawable churn for the flag to protect against anyway.
    func freezeForTransition() {
        isPaused = true
    }

    /// One fresh frame at the settled geometry, presented inside the same
    /// CATransaction as the geometry, so the compositor cannot show a surface that
    /// belongs to the old size.
    func settleAfterTransition() {
        isPaused = true     // still driven by needsDisplay, not by a timer

        // Take the final size from the settled view, and make MTKView act on it now
        // rather than at some later layout pass.
        layoutSubtreeIfNeeded()

        let backing = window?.backingScaleFactor ?? 1
        let size = CGSize(width: (bounds.width * backing).rounded(),
                          height: (bounds.height * backing).rounded())

        CATransaction.begin()
        CATransaction.setDisableActions(true)

        if size.width > 0, size.height > 0, size != drawableSize {
            drawableSize = size
        }

        // Transaction-tied present: presentsWithTransaction + waitUntilScheduled +
        // drawable.present(), never commandBuffer.present() (that is the async path).
        presentsWithTransaction = true
        draw()
        presentsWithTransaction = false

        CATransaction.commit()

        needsDisplay = true
    }

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        controller?.handleKeyDown(event)
    }

    override func keyUp(with event: NSEvent) {
        controller?.handleKeyUp(event)
    }

    override func flagsChanged(with event: NSEvent) {
        controller?.handleFlagsChanged(event)
    }

    /// Never let AppKit beep at unhandled keys - the emulator consumes everything.
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        // Command chords stay with the menu bar (see ZXKeyboardMap for why).
        false
    }

    override func resignFirstResponder() -> Bool {
        controller?.releaseAllKeys()
        return super.resignFirstResponder()
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()

        NotificationCenter.default.removeObserver(self)
        guard let window else { return }

        window.makeFirstResponder(self)

        // Focus loss is the classic way to strand a held modifier in the matrix
        for name in [NSWindow.didResignKeyNotification, NSWindow.didMiniaturizeNotification] {
            NotificationCenter.default.addObserver(self,
                                                   selector: #selector(handleFocusLoss),
                                                   name: name,
                                                   object: window)
        }
        NotificationCenter.default.addObserver(self,
                                               selector: #selector(handleFocusLoss),
                                               name: NSApplication.didResignActiveNotification,
                                               object: nil)
    }

    @objc private func handleFocusLoss() {
        controller?.releaseAllKeys()
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }
}

// MARK: - SwiftUI wrapper

struct MetalScreenView: NSViewRepresentable {
    @ObservedObject var controller: EmulatorController
    var windowManager: WindowManager?

    func makeNSView(context: Context) -> EmulatorMetalView {
        let view = EmulatorMetalView(controller: controller)
        view.windowManager = windowManager
        windowManager?.pictureView = view
        return view
    }

    func updateNSView(_ nsView: EmulatorMetalView, context: Context) {
        nsView.controller = controller
        nsView.windowManager = windowManager
        windowManager?.pictureView = nsView
        nsView.needsDisplay = true
    }
}
