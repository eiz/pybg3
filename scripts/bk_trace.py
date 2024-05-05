import lldb


def _on_after_match_distance(frame, bp_loc, dict):
    thread = frame.GetThread()
    match_dist = int(frame.FindRegister("x10").GetValue(), 16)
    if match_dist == 3508:
        enable_trace(lldb.debugger.GetSelectedTarget(), 0x1004AAB30)
        enable_trace(lldb.debugger.GetSelectedTarget(), 0x1004AA868)
        # enable_trace(lldb.debugger.GetSelectedTarget(), 0x1004AA97C)


def _on_cached_match_distance(frame, bp_loc, dict):
    thread = frame.GetThread()
    match_dist = int(frame.FindRegister("x10").GetValue(), 16)
    bits1 = int(frame.FindRegister("x26").GetValue(), 16)
    bits2 = int(frame.FindRegister("x28").GetValue(), 16)


#    if match_dist == 286152 and bits1 == 0x000144EF and bits2 == 0x001F27F8:


def _on_main_loop(frame, bp_loc, dict):
    thread = frame.GetThread()
    x26 = int(frame.FindRegister("x26").GetValue(), 16)
    x24 = int(frame.FindRegister("x24").GetValue(), 16)
    print(f"{x26:08X} {x24:08X}")


def _print_decoder_state(location, reg_name, frame):
    reg = frame.FindRegister(reg_name).GetValueAsUnsigned()
    target = lldb.debugger.GetSelectedTarget()
    process = target.GetProcess()
    data = process.ReadMemory(reg, 0x10, lldb.SBError())
    dst_start = int.from_bytes(data[0:8], byteorder="little")
    dst_cur = int.from_bytes(data[8:16], byteorder="little")
    print(
        f"{location} dst_cur {dst_cur:016X} dst_start {dst_start:016X} offset {dst_cur - dst_start:08X}"
    )


def _on_entry(frame, bp_loc, dict):
    return
    _print_decoder_state("entry", "x0", frame)


def _on_end_of_quantum(frame, bp_loc, dict):
    _print_decoder_state("quantum", "x20", frame)


TARGET_LIB = "Baldur's Gate 3"
TRACE_POINTS = {
    0x1004AAC74: {
        "format": "after match distance {} states {:08X} {:08X}",
        "regs": ["x10", "x26", "x8"],
        "callback": _on_after_match_distance,
    },
    0x1004AAB30: {
        "format": "cached match distance {} states {:08X} {:08X}",
        "regs": ["x10", "x26", "x28"],
        "callback": _on_cached_match_distance,
        "lazy": True,
    },
    0x1004AA8C0: {
        "format": "lit {}",
        "regs": ["x23"],
        "lazy": True,
    },
    0x1004AA97C: {
        "format": "lit {}",
        "regs": ["x23"],
        "lazy": True,
    },
    0x1004AA868: {
        "callback": _on_main_loop,
        "lazy": True,
    },
    0x1004AA510: {
        "callback": _on_entry,
    },
    0x1004AAE54: {
        "callback": _on_end_of_quantum,
    },
}


def set_initial_breakpoint():
    global INITIAL_BREAKPOINT
    target = lldb.debugger.GetSelectedTarget()
    breakpoint = target.BreakpointCreateByName("bg3_granny_decompress_bitknit")
    breakpoint.SetScriptCallbackFunction("bk_trace.initial_breakpoint_callback")
    breakpoint.SetOneShot(True)


def initial_breakpoint_callback(frame, bp_loc, dict):
    set_address_breakpoints()
    frame.GetThread().GetProcess().Continue()


def set_address_breakpoints():
    target = lldb.debugger.GetSelectedTarget()
    for address, info in TRACE_POINTS.items():
        if not info.get("lazy", False):
            enable_trace(target, address)


def enable_trace(target, address):
    shlib = TARGET_LIB
    module = target.FindModule(lldb.SBFileSpec(shlib))
    if module:
        sb_address = module.ResolveFileAddress(address)
        breakpoint = target.BreakpointCreateBySBAddress(sb_address)
        breakpoint.SetScriptCallbackFunction("bk_trace.address_breakpoint_callback")


def address_breakpoint_callback(frame, bp_loc, dict):
    thread = frame.GetThread()
    info = TRACE_POINTS[bp_loc.GetAddress().GetFileAddress()]
    if "format" in info:
        print(
            info["format"].format(
                *[
                    int(frame.FindRegister(reg).GetValue(), 16)
                    for reg in info.get("regs", [])
                ]
            )
        )
    if "callback" in info:
        info["callback"](frame, bp_loc, dict)
    if not info.get("break", False):
        thread.GetProcess().Continue()


def __lldb_init_module(debugger, dict):
    set_initial_breakpoint()
