// What macOS shows (colors.py): the first frame of each movie through AVAssetImageGenerator, ColorSync applying the
// file's tags, drawn into an sRGB bitmap; prints every pixel of the middle row as RRGGBB
import AVFoundation
import CoreGraphics

/// The frame at time zero (copyCGImage is deprecated from macOS 15: the asynchronous call, waited for)
func frame(_ gen: AVAssetImageGenerator) throws -> CGImage {
    var result: CGImage?
    var failure: Error?
    let done = DispatchSemaphore(value: 0)
    gen.generateCGImageAsynchronously(for: .zero) { image, _, error in
        result = image
        failure = error
        done.signal()
    }
    done.wait()
    if let image = result { return image }
    throw failure ?? NSError(domain: "macshow", code: 1)
}
for path in CommandLine.arguments.dropFirst() {
    let gen = AVAssetImageGenerator(asset: AVURLAsset(url: URL(fileURLWithPath: path)))
    gen.requestedTimeToleranceBefore = .zero
    gen.requestedTimeToleranceAfter = .positiveInfinity
    guard let img = try? frame(gen) else { print(path, "no frame"); continue }
    let w = img.width, h = img.height
    var px = [UInt8](repeating: 0, count: w * h * 4)
    let ctx = CGContext(data: &px, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                        space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
    ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h))
    let y = h / 2
    var row = [String]()
    for x in 0..<w { let i = (y * w + x) * 4; row.append(String(format: "%02X%02X%02X", px[i], px[i + 1], px[i + 2])) }
    print(path.split(separator: "/").last!, row.joined(separator: " "))
}
