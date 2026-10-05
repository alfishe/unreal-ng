    // Slot change reply (SlotControl's envelope over SlotChangeResult; ZX-bus slots architecture.md §9)
    schemas["SlotChangeReply"]["type"] = "object";
    schemas["SlotChangeReply"]["properties"]["ok"]["type"] = "boolean";
    schemas["SlotChangeReply"]["properties"]["status"]["type"] = "string";
    schemas["SlotChangeReply"]["properties"]["status"]["enum"].append("applied");
    schemas["SlotChangeReply"]["properties"]["status"]["enum"].append("dry-run");
    schemas["SlotChangeReply"]["properties"]["status"]["enum"].append("refused");
    schemas["SlotChangeReply"]["properties"]["status"]["enum"].append("recording");
    schemas["SlotChangeReply"]["properties"]["status"]["enum"].append("no-machine");
    schemas["SlotChangeReply"]["properties"]["status"]["enum"].append("failed");
    schemas["SlotChangeReply"]["properties"]["status"]["enum"].append("bad-request");
    schemas["SlotChangeReply"]["properties"]["message"]["type"] = "string";
    schemas["SlotChangeReply"]["properties"]["message"]["description"] =
        "The refusal ('refused: ...', 'needs replaceIfIncompatible: ...', the TTD session) or the error";
    schemas["SlotChangeReply"]["properties"]["op"]["type"] = "string";
    schemas["SlotChangeReply"]["properties"]["slot"]["type"] = "string";
    schemas["SlotChangeReply"]["properties"]["card"]["type"] = "string";
    schemas["SlotChangeReply"]["properties"]["emulatorId"]["type"] = "string";
    schemas["SlotChangeReply"]["properties"]["emulatorId"]["description"] = "The machine now (the new id when applied)";
    schemas["SlotChangeReply"]["properties"]["plan"]["$ref"] = "#/components/schemas/SlotPlan";
    schemas["SlotChangeReply"]["properties"]["restart"]["type"] = "object";
    schemas["SlotChangeReply"]["properties"]["restart"]["description"] =
        "restarted, previousEmulatorId, emulatorId, wasRunning, started (the new machine runs when the old one ran)";
    schemas["SlotChangeReply"]["properties"]["media"]["type"] = "object";
    schemas["SlotChangeReply"]["properties"]["media"]["description"] =
        "Where the media went: attached (same slot on the new machine), detached, closed, lines";
    schemas["SlotChangeReply"]["properties"]["stranded"]["type"] = "array";
    schemas["SlotChangeReply"]["properties"]["stranded"]["description"] = "Unsaved media of removed cards (slot, source, changes)";

    schemas["SlotPlan"]["type"] = "object";
    schemas["SlotPlan"]["description"] =
        "The plan engine's plan (slots::SlotPlan) with the change's verdict: allowed, refusal, recording; "
        "needsReplaceIfIncompatible, hardRefusal; op, slot, card, options, fit (real / adapter / unrealistic), "
        "adapter, missingSignals, arbitration; reasons[] (rule D1-D12, hard, text); removed[] (slot, card, options, "
        "rule, reason, clashing functions, replacedInSlot, undo: the plug that puts it back); shadowed[] (device, by, "
        "ports); builtInSwitchedOff[]; removedFromSocket[]; disabled[]; deadPorts[]; busFights[]; lostFunctions[]; "
        "media[] (slot, card, mediaSlot, dirty, disposition); resultingSlots[]; slotsSection[] (the [SLOTS] lines of "
        "the restarted machine); lines[] (for people)";
    schemas["SlotPlan"]["properties"]["allowed"]["type"] = "boolean";
    schemas["SlotPlan"]["properties"]["refusal"]["type"] = "string";
    schemas["SlotPlan"]["properties"]["removed"]["type"] = "array";
    schemas["SlotPlan"]["properties"]["removed"]["items"]["type"] = "object";
    schemas["SlotPlan"]["properties"]["lines"]["type"] = "array";
    schemas["SlotPlan"]["properties"]["lines"]["items"]["type"] = "string";

    schemas["SlotCarry"]["type"] = "object";
    schemas["SlotCarry"]["description"] =
        "What a model switch did with the cards (R-OP-9): carried, kept[] (\"zxbus.2 -> edge.2 = soundrive\"), "
        "dropped[] (slot, card, reason), lines[]";
