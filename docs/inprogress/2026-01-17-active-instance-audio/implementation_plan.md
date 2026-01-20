# Audio Binding on Tile Focus

When an emulator tile receives focus, audio should be bound to that instance (with sound temporarily enabled). When focus switches to another tile, the previous tile's audio must be unbound and the new one bound.

## Proposed Changes

### VideoWallWindow

#### [MODIFY] [VideoWallWindow.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-videowall/src/videowall/VideoWallWindow.h)

Add sound manager and audio binding machinery:

- Add `#include` for [AppSoundManager](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/src/emulator/soundmanager.h#13-49) (copy from [unreal-qt/src/emulator/soundmanager.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/src/emulator/soundmanager.h))
- Add private member `AppSoundManager* _soundManager = nullptr;`
- Add private member `EmulatorTile* _audioBoundTile = nullptr;` to track currently bound tile
- Add private slot `void onTileFocused(EmulatorTile* tile);`
- Add private method `void bindAudioToTile(EmulatorTile* tile);`

#### [MODIFY] [VideoWallWindow.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-videowall/src/videowall/VideoWallWindow.cpp)

Implement audio binding:

1. **Constructor**: Initialize sound manager (similar to [MainWindow](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/src/mainwindow.cpp#32-212)):
   ```cpp
   _soundManager = new AppSoundManager();
   if (_soundManager->init())
   {
       _soundManager->start();
   }
   ```

2. **Destructor**: Clean up sound manager:
   ```cpp
   if (_soundManager)
   {
       _soundManager->stop();
       _soundManager->deinit();
       delete _soundManager;
   }
   ```

3. **`bindAudioToTile(EmulatorTile* tile)`**: Core binding logic:
   ```cpp
   void VideoWallWindow::bindAudioToTile(EmulatorTile* tile)
   {
       if (!_soundManager || !tile || !tile->emulator())
           return;

       // Unbind from previous tile
       if (_audioBoundTile && _audioBoundTile != tile)
       {
           auto* prevEmulator = _audioBoundTile->emulator().get();
           if (prevEmulator)
           {
               prevEmulator->ClearAudioCallback();
               // Disable sound generation for previous tile
               if (auto* fm = prevEmulator->GetFeatureManager())
               {
                   fm->setFeature(Features::kSoundGeneration, false);
               }
           }
       }

       // Bind to new tile
       auto emulator = tile->emulator();
       if (emulator)
       {
           // Enable sound generation for focused tile
           if (auto* fm = emulator->GetFeatureManager())
           {
               fm->setFeature(Features::kSoundGeneration, true);
           }
           // Set audio callback
           emulator->SetAudioCallback(_soundManager, &AppSoundManager::audioCallback);
       }

       _audioBoundTile = tile;
       qDebug() << "Audio bound to tile:" << QString::fromStdString(emulator->GetUUID());
   }
   ```

4. **`onTileFocused(EmulatorTile* tile)`** slot: Connect to `EmulatorTile::tileFocused` signal:
   ```cpp
   void VideoWallWindow::onTileFocused(EmulatorTile* tile)
   {
       bindAudioToTile(tile);
   }
   ```

5. **[addEmulatorTile()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-videowall/src/videowall/VideoWallWindow.cpp#117-143)**: Connect new tile's focus signal:
   ```cpp
   connect(tile, &EmulatorTile::tileFocused, this, &VideoWallWindow::onTileFocused);
   ```

---

### EmulatorTile

#### [MODIFY] [EmulatorTile.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-videowall/src/videowall/EmulatorTile.h)

Add focus signal:

```cpp
signals:
    void tileFocused(EmulatorTile* tile);
```

#### [MODIFY] [EmulatorTile.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-videowall/src/videowall/EmulatorTile.cpp)

Emit signal on focus:

```cpp
void EmulatorTile::focusInEvent(QFocusEvent* event)
{
    _hasTileFocus = true;
    emit tileFocused(this);  // <-- ADD THIS
    update();
    QWidget::focusInEvent(event);
}
```

---

### CMake Integration

#### [MODIFY] [CMakeLists.txt](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-videowall/CMakeLists.txt)

Add soundmanager source file (copied from unreal-qt or linked):

```cmake
# Add soundmanager source (if copying)
set(SOURCES
    ${SOURCES}
    src/emulator/soundmanager.cpp
)
```

Or link to shared library if soundmanager becomes a shared component.

## Verification Plan

### Manual Verification

1. **Build videowall**: `cmake --build cmake-build-debug --target unreal-videowall`
2. **Run videowall**: Launch `unreal-videowall`
3. **Create 2+ tiles**: Press `Cmd+N` twice to create two emulator tiles
4. **Click first tile**: Observe audio should start playing from the first emulator
5. **Click second tile**: Observe:
   - Audio from first tile should **stop**
   - Audio from second tile should **start**
6. **Repeat switching**: Click between tiles multiple times to verify clean handoff
7. **Load a tape/snapshot with sound**: Drag a game with beeper/AY to each tile and verify audio follows focus

> [!NOTE]
> There are no existing automated tests for the videowall module. Since this is a UI/audio feature, manual verification is appropriate.
