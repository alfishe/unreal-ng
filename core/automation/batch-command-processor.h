/// @file batch_command_processor.h
/// @brief Batch command execution infrastructure for parallel multi-emulator operations
/// @date 2026-01-13
///
/// Provides a thread-pooled batch processor for executing commands across multiple
/// emulator instances in parallel. Used by WebAPI and CLI automation interfaces.
///
/// @see docs/emulator/design/control-interfaces/batch-command-execution.md

#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class Emulator;
class EmulatorManager;

/// @brief A single command to execute in a batch
/// @details Commands are validated against the batchable command list before execution.
///          Non-batchable commands (e.g., step, stepover) will be rejected.
struct BatchCommand
{
    std::string emulatorId;  ///< Target emulator ID (UUID, symbolic ID, or numeric index)
    std::string command;     ///< Command name: "load-snapshot", "reset", "pause", "resume", "feature"
    std::string arg1;        ///< First argument (e.g., file path for load-snapshot, feature name)
    std::string arg2;        ///< Second argument (e.g., "on"/"off" for feature command)
};

/// @brief Result of a single batch command execution
struct BatchCommandResult
{
    std::string emulatorId;  ///< Emulator ID the command was executed on
    std::string command;     ///< Command that was executed
    bool success = false;    ///< True if command succeeded
    std::string error;       ///< Error message if failed (empty on success)
};

/// @brief Aggregated result of entire batch execution
/// @details Contains per-command results, timing, and success/failure counts.
struct BatchResult
{
    bool success = false;                     ///< True if ALL commands succeeded (failed == 0)
    int total = 0;                            ///< Total number of commands in batch
    int succeeded = 0;                        ///< Number of commands that succeeded
    int failed = 0;                           ///< Number of commands that failed
    double durationMs = 0.0;                  ///< Total execution time in milliseconds
    std::vector<BatchCommandResult> results;  ///< Per-command results (same order as input)
};

/// @brief Batch command processor with fixed thread pool
///
/// Executes multiple commands across emulator instances in parallel using a
/// fixed-size thread pool (default 4 threads, optimal for Apple Silicon).
///
/// ## Supported Commands
/// - `load-snapshot` - Load snapshot file (arg1 = path)
/// - `reset` - Reset emulator to initial state
/// - `pause` - Pause emulator execution
/// - `resume` - Resume emulator execution
/// - `feature` - Set feature state (arg1 = name, arg2 = "on"/"off")
/// - `create` - Create new emulator instance
/// - `start` - Start emulator execution
/// - `stop` - Stop emulator execution
///
/// ## Thread Safety
/// - Thread-safe for concurrent Execute() calls
/// - Uses existing emulator Pause()/Resume() for memory safety
/// - No internal locking required (each command targets different emulator)
///
/// ## Performance
/// Benchmarked on Apple M1 Ultra:
/// - 48 instances: ~2.5 ms
/// - 100 instances: ~5.5 ms
/// - 180 instances: ~10 ms
///
/// ## Example Usage
/// @code
/// BatchCommandProcessor processor(&emulatorManager, 4);
///
/// std::vector<BatchCommand> commands = {
///     {"emu-001", "load-snapshot", "/path/to/game1.sna", ""},
///     {"emu-002", "load-snapshot", "/path/to/game2.sna", ""},
///     {"emu-003", "reset", "", ""},
///     {"0", "feature", "sound", "off"},  // By index
/// };
///
/// BatchResult result = processor.Execute(commands);
///
/// if (result.success) {
///     printf("All %d commands succeeded in %.2f ms\n", result.total, result.durationMs);
/// } else {
///     printf("%d/%d commands failed\n", result.failed, result.total);
/// }
/// @endcode
class BatchCommandProcessor
{
public:
    /// @brief Default thread pool size (optimal for Apple Silicon 4-8 efficiency cores)
    static constexpr int DEFAULT_THREAD_COUNT = 4;

    /// @brief Construct batch processor with emulator manager
    /// @param manager Pointer to EmulatorManager (must outlive processor)
    /// @param threadCount Number of worker threads (default: 4)
    explicit BatchCommandProcessor(EmulatorManager* manager, int threadCount = DEFAULT_THREAD_COUNT);

    /// @brief Destructor
    ~BatchCommandProcessor();

    /// @brief Execute a batch of commands in parallel
    /// @param commands Vector of commands to execute
    /// @return BatchResult with per-command results, timing, and success counts
    /// @note Commands execute in parallel; result order matches input order
    /// @note Non-batchable commands are rejected with error (not executed)
    BatchResult Execute(const std::vector<BatchCommand>& commands);

    /// @brief Check if a command is batchable (safe for parallel execution)
    /// @param command Command name to check
    /// @return True if command can be executed in batch
    /// @note State-dependent commands (step, stepover) are NOT batchable
    static bool IsBatchable(const std::string& command);

    /// @brief Get list of all batchable command names
    /// @return Vector of command names that can be used in batch
    static std::vector<std::string> GetBatchableCommands();

private:
    /// @brief Execute a single command on target emulator
    /// @param cmd Command to execute
    /// @return Result with success/failure and error message
    BatchCommandResult ExecuteSingle(const BatchCommand& cmd);

    EmulatorManager* _manager;  ///< Emulator manager (not owned)
    int _threadCount;           ///< Number of worker threads
};
