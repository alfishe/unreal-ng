-- POC 050: section timing on exec taps. One chunk per marker address; every chunk shares the global ST of the
-- emulator's hook state. The driver fills in the placeholders (frame length, acknowledge, events) per address.
--
-- A tap fires at the start of the instruction at its address: hit.frame * FT + hit.t is the global T there, the
-- clock the reference script reads after POST /steps stopped at a breakpoint.
ST = ST or {
    ft = @FT@,               -- T-states per frame
    ack = @ACK@,             -- the interrupt acknowledge, T (13 IM 1, 19 IM 2: the reference's constants)
    stack = {},              -- open passes, innermost last: {s, start, frame, t, ints, int_t}
    pending = {},            -- accepted interrupts not returned from yet: {sp, start, inside}
    passes = {},             -- flat: section, frame, t, elapsed, net, interrupts, ... (6 numbers per pass)
    stats = {},              -- per section: count, min, max, sum, net_max
    entered = {},
    orphan = 0, dropped = 0, hits = 0,
}

local function now(hit) return hit.frame * ST.ft + hit.t end

local function entry(s, hit)
    local st = ST.stack
    for i = 1, #st do             -- entered again while open: the open pass left without its exit
        if st[i].s == s then
            ST.dropped = ST.dropped + #st - i + 1
            for j = #st, i, -1 do st[j] = nil end
            break
        end
    end
    st[#st + 1] = {s = s, start = now(hit), frame = hit.frame, t = hit.t, ints = 0, int_t = 0}
    ST.entered[s] = true
end

local function exit(s, hit)
    local st = ST.stack
    for i = #st, 1, -1 do
        if st[i].s == s then
            local o = st[i]
            ST.dropped = ST.dropped + #st - i
            for j = #st, i, -1 do st[j] = nil end
            local elapsed = now(hit) - o.start
            local net = elapsed - o.int_t
            local p = ST.passes
            local n = #p
            p[n + 1], p[n + 2], p[n + 3], p[n + 4], p[n + 5], p[n + 6] = s, o.frame, o.t, elapsed, net, o.ints
            local x = ST.stats[s]
            if not x then
                x = {count = 0, min = elapsed, max = elapsed, sum = 0, net_max = net}
                ST.stats[s] = x
            end
            x.count = x.count + 1
            x.sum = x.sum + elapsed
            if elapsed < x.min then x.min = elapsed end
            if elapsed > x.max then x.max = elapsed end
            if net > x.net_max then x.net_max = net end
            return
        end
    end
    if ST.entered[s] then ST.orphan = ST.orphan + 1 end
end

-- The interrupt lands here: it was accepted one acknowledge earlier. SP points at the return address; the
-- handler's exit runs with the same SP
local function landing(hit)
    local inside = {}
    for i = 1, #ST.stack do
        local o = ST.stack[i]
        o.ints = o.ints + 1
        inside[i] = o
    end
    ST.pending[#ST.pending + 1] = {sp = hit.regs.sp, start = now(hit) - ST.ack, inside = inside}
end

-- The handler's return instruction starts here; the interrupted instruction starts `len` T-states later
local function handler_exit(hit, len)
    local pd = ST.pending
    local p = pd[#pd]
    if p and p.sp == hit.regs.sp then
        pd[#pd] = nil
        local cost = now(hit) + len - p.start
        for i = 1, #p.inside do
            local o = p.inside[i]
            o.int_t = o.int_t + cost
        end
    end
end

local function events(hit)
    @EVENTS@
end

-- sync: the hits come in machine order, one at a time. frame and deferred: each tap has its own ring and the
-- rings are drained one tap after another, so the hits are kept and run in machine order by ST.flush
ST.raw = ST.raw or {}
function ST.flush()
    local raw = ST.raw
    table.sort(raw, function(x, y) return x[1] < y[1] end)
    for i = 1, #raw do raw[i][3](raw[i][2]) end
    ST.raw = {}
end
local ordered = @ORDERED@
return function(hit)
    ST.hits = ST.hits + 1
    if ordered then
        events(hit)
    else
        local raw = ST.raw
        raw[#raw + 1] = {hit.frame * ST.ft + hit.t, {frame = hit.frame, t = hit.t, regs = {sp = hit.regs.sp}}, events}
    end
end
