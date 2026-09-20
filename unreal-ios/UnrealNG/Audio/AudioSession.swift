import Foundation
import AVFoundation
import AudioToolbox

final class AudioSessionManager {
    static let shared = AudioSessionManager()

    private var audioUnit: AudioComponentInstance?
    private var isRunning = false
    private var deviceSampleRate: Double = 48000

    private init() {}

    func setupAudioSession() {
        let session = AVAudioSession.sharedInstance()
        do {
            try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try session.setPreferredIOBufferDuration(0.005)
            try session.setPreferredSampleRate(48000)
            try session.setActive(true)

            deviceSampleRate = session.sampleRate
            print("AudioSessionManager: Device sample rate = \(deviceSampleRate) Hz")
        } catch {
            print("AudioSessionManager: Failed to configure AVAudioSession: \(error)")
        }

        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleInterruption),
            name: AVAudioSession.interruptionNotification,
            object: session
        )

        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleRouteChange),
            name: AVAudioSession.routeChangeNotification,
            object: session
        )
    }

    func startAudioUnit() {
        guard audioUnit == nil else { return }

        var desc = AudioComponentDescription(
            componentType: kAudioUnitType_Output,
            componentSubType: kAudioUnitSubType_RemoteIO,
            componentManufacturer: kAudioUnitManufacturer_Apple,
            componentFlags: 0,
            componentFlagsMask: 0
        )

        guard let component = AudioComponentFindNext(nil, &desc) else {
            print("AudioSessionManager: Failed to find RemoteIO component")
            return
        }

        var au: AudioComponentInstance?
        guard AudioComponentInstanceNew(component, &au) == noErr, let unit = au else {
            print("AudioSessionManager: Failed to create AudioUnit")
            return
        }
        audioUnit = unit

        var streamFormat = AudioStreamBasicDescription(
            mSampleRate: deviceSampleRate,
            mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
            mBytesPerPacket: 8,
            mFramesPerPacket: 1,
            mBytesPerFrame: 8,
            mChannelsPerFrame: 2,
            mBitsPerChannel: 32,
            mReserved: 0
        )

        AudioUnitSetProperty(
            unit,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input,
            0,
            &streamFormat,
            UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
        )

        var callbackStruct = AURenderCallbackStruct(
            inputProc: audioRenderCallback,
            inputProcRefCon: Unmanaged.passUnretained(self).toOpaque()
        )

        AudioUnitSetProperty(
            unit,
            kAudioUnitProperty_SetRenderCallback,
            kAudioUnitScope_Input,
            0,
            &callbackStruct,
            UInt32(MemoryLayout<AURenderCallbackStruct>.size)
        )

        guard AudioUnitInitialize(unit) == noErr else {
            print("AudioSessionManager: Failed to initialize AudioUnit")
            return
        }

        let rate = UInt32(deviceSampleRate)
        if UNGBridge.shared().attachAudioPull(withSampleRate: rate) {
            print("AudioSessionManager: Attached pull audio at \(rate) Hz")
        }

        guard AudioOutputUnitStart(unit) == noErr else {
            print("AudioSessionManager: Failed to start AudioUnit")
            return
        }

        isRunning = true
        UNGBridge.shared().setAudioActive(true)
        print("AudioSessionManager: AudioUnit started")
    }

    func stopAudioUnit() {
        guard let unit = audioUnit else { return }

        UNGBridge.shared().setAudioActive(false)
        AudioOutputUnitStop(unit)
        AudioUnitUninitialize(unit)
        AudioComponentInstanceDispose(unit)
        audioUnit = nil
        isRunning = false
        print("AudioSessionManager: AudioUnit stopped")
    }

    @objc private func handleInterruption(notification: Notification) {
        guard let userInfo = notification.userInfo,
              let typeValue = userInfo[AVAudioSessionInterruptionTypeKey] as? UInt,
              let type = AVAudioSession.InterruptionType(rawValue: typeValue) else {
            return
        }

        switch type {
        case .began:
            UNGBridge.shared().setAudioActive(false)
        case .ended:
            if let optionsValue = userInfo[AVAudioSessionInterruptionOptionKey] as? UInt {
                let options = AVAudioSession.InterruptionOptions(rawValue: optionsValue)
                if options.contains(.shouldResume) {
                    try? AVAudioSession.sharedInstance().setActive(true)
                    UNGBridge.shared().setAudioActive(true)
                }
            }
        @unknown default:
            break
        }
    }

    @objc private func handleRouteChange(notification: Notification) {
        let session = AVAudioSession.sharedInstance()
        let newRate = session.sampleRate
        if newRate != deviceSampleRate {
            print("AudioSessionManager: Sample rate changed \(deviceSampleRate) -> \(newRate)")
            deviceSampleRate = newRate
            if isRunning {
                stopAudioUnit()
                startAudioUnit()
            }
        }
    }
}

private func audioRenderCallback(
    inRefCon: UnsafeMutableRawPointer,
    ioActionFlags: UnsafeMutablePointer<AudioUnitRenderActionFlags>,
    inTimeStamp: UnsafePointer<AudioTimeStamp>,
    inBusNumber: UInt32,
    inNumberFrames: UInt32,
    ioData: UnsafeMutablePointer<AudioBufferList>?
) -> OSStatus {
    guard let bufferList = ioData else { return noErr }

    let buffer = UnsafeMutableAudioBufferListPointer(bufferList)[0]
    guard let data = buffer.mData else { return noErr }

    let floatPtr = data.assumingMemoryBound(to: Float.self)
    let frames = Int(inNumberFrames)

    let pulled = UNGBridge.shared().pullAudioF32(floatPtr, frames: frames)

    if pulled < frames {
        let offset = pulled * 2
        let remaining = (frames - pulled) * 2
        floatPtr.advanced(by: offset).assign(repeating: 0, count: remaining)
    }

    return noErr
}
