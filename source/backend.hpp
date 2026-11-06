#pragma once
#include <switch.h>
#include <array>
#include <string>
#include <vector>

struct JoyTarget {
    HidsysUniquePadId padId{};
    BtdrvAddress address{};
    std::string name;
    u8 side = 0;
    bool wired = false;
    bool colorsKnown = false;
    u32 bodyColor = 0;
    u32 buttonsColor = 0;
    std::string status = "Connected via Bluetooth";
    bool failed = false;
    bool done = false;
    int phase = 0;
    u8 counter = 0;
    u8 identityRetries = 0;
    bool attempted = false;
    u64 sentTick = 0;
    u64 deadline = 0;
};

class JoyBackend {
    bool hidReady = false;
    bool btmReady = false;
    bool driverReady = false;
    bool running = false;
    bool identifying = false;
    int colorPhase = 0;
    bool colorBackupOnly = false;
    size_t colorTarget = 0;
    std::array<u8, 6> requestedColors{};
    std::string colorMessage = "Select a wireless Joy-Con. Colors are saved permanently to the controller.";
    std::string colorBackup;
    SharedMemory memory{};
    bool mapped = false;
    const BtdrvCircularBuffer* ring = nullptr;
    size_t cursor = 0;
    std::vector<JoyTarget> targets;
    std::string message;
    std::string diagnostics;
    bool logAvailable = false;
    u32 observedPackets = 0;
    u32 observedData = 0;
    u32 observedReplies = 0;
    u32 ringProblems = 0;
    std::string connectionSnapshot;
    bool snapshotValid = false;
    u64 nextConnectionCheck = 0;

    Result Open();
    bool ReadConnections(std::string& snapshot);
    bool Observe();
    bool Send(JoyTarget& target, u8 command, u8 argument);
    void Receive();
    void Response(const BtdrvHidReportEventInfo& event, size_t size, u64 tick);
    void Fail(JoyTarget& target, const char* text, Result rc = 0);
    void SendColorCommand();
    void ColorResponse(const u8* reply, size_t size);
    void FinishColor(const char* text);
    void BeginColors(HidsysUniquePadId padId, u32 body, u32 buttons, bool backupOnly);
public:
    ~JoyBackend();
    void Refresh();
    void Start();
    void ApplyColors(HidsysUniquePadId padId, u32 body, u32 buttons);
    void BackupColors(HidsysUniquePadId padId);
    bool ReadColorBackup(HidsysUniquePadId padId, const std::string& path, std::array<u8, 6>& colors);
    void RestoreColors(HidsysUniquePadId padId, const std::string& path, const std::array<u8, 6>& preview);
    const std::string& ColorMessage() const { return colorMessage; }
    void Update();
    void AutoRefresh(bool paused);
    void ExplainUnavailable();
    void SetLogging(bool enabled);
    bool LoggingEnabled() const;
    bool Running() const { return running; }
    bool Ready() const;
    const std::vector<JoyTarget>& Targets() const { return targets; }
    const std::string& Message() const { return message; }
    const std::string& Diagnostics() const { return diagnostics; }
    bool LogAvailable() const { return logAvailable; }
};
