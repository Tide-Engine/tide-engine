# How LLDB shows a Tide game's values: the part every game shares. tidec embeds
# this file in <name>_lldb.py and writes the game's tables after it
# (TIDE_TYPES, TIDE_ENUMS, TIDE_LOCAL_LISTS, TIDE_LOCALS, and the runtime's constants), so this
# reads them at call time. See compiler/src/debug_views_codegen.inc.
#
# Text, lists and maps are handles (tide/text.h): an offset, and in its top
# two bits which heap it's in. The heaps code runs with are thread-locals of
# the stopped thread, tide_world_heaps and tide_scratch_slots, which LLDB
# reads on macOS and Linux (Windows' LLDB can't: use a natvis debugger there).

import lldb

_registered = {}  # A struct field's load address: what kind of container it is (TideStruct)
_layouts = {}


def _frame(valobj):
    frame = valobj.GetFrame()
    if frame and frame.IsValid():
        return frame
    return valobj.GetProcess().GetSelectedThread().GetSelectedFrame()


def _thread_local(valobj, name):
    frame = _frame(valobj)
    for kind in (lldb.eValueTypeVariableThreadLocal, lldb.eValueTypeVariableGlobal):
        v = frame.FindValue(name, kind)
        if v and v.IsValid():
            return v
    v = frame.GetValueForVariablePath(name)
    if v and v.IsValid():
        return v
    return None


def _layout(target, type_name):
    """A struct's size and its fields' offsets, from the debug info."""
    if type_name in _layouts:
        return _layouts[type_name]
    t = target.FindFirstType(type_name)
    if not t.IsValid():
        return None
    t = t.GetCanonicalType()
    fields = {}
    for i in range(t.GetNumberOfFields()):
        f = t.GetFieldAtIndex(i)
        fields[f.GetName()] = f.GetOffsetInBytes()
    _layouts[type_name] = (t.GetByteSize(), fields)
    return _layouts[type_name]


def _read(process, address, size):
    error = lldb.SBError()
    value = process.ReadUnsignedFromMemory(address, size, error)
    return value if error.Success() else None


def _pointer(process, address):
    error = lldb.SBError()
    value = process.ReadPointerFromMemory(address, error)
    return value if error.Success() else None


def _heap_page(valobj, where, offset):
    """The address of the data of the page `offset` is in, and where that page starts, in heap `where`."""
    process = valobj.GetProcess()
    target = valobj.GetTarget()
    heaps = _thread_local(valobj, "tide_world_heaps")
    if heaps is None:
        return None
    heap = heaps.GetChildAtIndex(where).GetValueAsUnsigned()
    heap_layout = _layout(target, "tide_heap")
    page_layout = _layout(target, "tide_page")
    if not heap or heap_layout is None or page_layout is None:
        return None
    table = _pointer(process, heap + heap_layout[1]["page"])
    if not table:
        return None
    page = _pointer(process, table + (offset >> TIDE_HEAP_PAGE_SHIFT) * process.GetAddressByteSize())
    if not page:
        return None
    first = _read(process, page + page_layout[1]["first"], 4)
    if first is None:
        return None
    return page + page_layout[0], first << TIDE_HEAP_PAGE_SHIFT


def _block(valobj, at):
    """The address of the block a handle names (tide_block_at), or None."""
    if not at:
        return None
    offset = at & 0x3FFFFFFF
    where = at >> 30
    if where == TIDE_IN_SCRATCH:
        slots = _thread_local(valobj, "tide_scratch_slots")
        if slots is None:
            return None
        base = slots.GetChildAtIndex(offset >> TIDE_SCRATCH_SHIFT).GetValueAsUnsigned()
        return base + (offset & (TIDE_SCRATCH_BYTES - 1)) if base else None
    page = _heap_page(valobj, where, offset)
    if page is None:
        return None
    return page[0] + (offset - page[1])


def _block_counts(valobj, block):
    layout = _layout(valobj.GetTarget(), "tide_block")
    process = valobj.GetProcess()
    return _read(process, block + layout[1]["a"], 4), _read(process, block + layout[1]["b"], 4), layout[0]


def _element(valobj, at, block, index, size):
    """The address of a list's element `index` of `size` bytes, side by side or in chunks (tide_list_at)."""
    count, b, header = _block_counts(valobj, block)
    if b & TIDE_LIST_CHUNKED == 0:
        return block + header + index * size
    per = TIDE_LIST_CHUNK // size
    chunk = _read(valobj.GetProcess(), block + header + (index // per) * 4, 4)
    if chunk is None:
        return None
    page = _heap_page(valobj, at >> 30, chunk)
    if page is None:
        return None
    return page[0] + (chunk - page[1]) + header + (index % per) * size


def _handle(valobj):
    return valobj.GetChildMemberWithName("at").GetValueAsUnsigned()


def entity_summary(valobj, internal_dict):
    index = valobj.GetChildMemberWithName("index").GetValueAsUnsigned()
    generation = valobj.GetChildMemberWithName("generation").GetValueAsUnsigned()
    return "none" if generation == 0 else "Entity(%d:%d)" % (index, generation)


def _quote(data):
    return '"' + data.decode("utf-8", "replace").replace("\\", "\\\\").replace('"', '\\"') + '"'


def str_summary(valobj, internal_dict):
    pointer = valobj.GetChildMemberWithName("ptr").GetValueAsUnsigned()
    length = valobj.GetChildMemberWithName("bytes").GetValueAsSigned()
    if not pointer or length <= 0:
        return '""'
    error = lldb.SBError()
    data = valobj.GetProcess().ReadMemory(pointer, min(length, 4096), error)
    return _quote(data) if error.Success() else "<unreadable>"


def text_summary(valobj, internal_dict):
    at = _handle(valobj)
    if not at:
        return '""'
    block = _block(valobj, at)
    if block is None:
        return "<text in a heap this thread can't see>"
    length, _, header = _block_counts(valobj, block)
    if not length:
        return '""'
    error = lldb.SBError()
    data = valobj.GetProcess().ReadMemory(block + header, min(length, 4096), error)
    return _quote(data) if error.Success() else "<unreadable>"


def _kind_of(valobj):
    """What a list or map value holds: a struct field's (TideStruct) or a local's (TIDE_LOCALS)."""
    address = valobj.GetLoadAddress()
    if address in _registered:
        return _registered[address]
    function = _frame(valobj).GetFunctionName() or ""
    return TIDE_LOCALS.get((function.split("(")[0], valobj.GetName()))


class TideList:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.update()

    def _list(self):
        return self.valobj

    def _kind(self):
        return _kind_of(self.valobj)

    def update(self):
        self.count = 0
        self.asset = False
        self.kind = self._kind()
        self.at = _handle(self._list())
        self.block = _block(self.valobj, self.at)
        if self.block is not None:
            count, b, _ = _block_counts(self.valobj, self.block)
            self.count = count or 0
            self.asset = b == TIDE_LIST_ASSET
        self.type = None
        if self.kind and self.kind[0] == "list":
            self.type = self.valobj.GetTarget().FindFirstType(self.kind[1])
        return False

    def has_children(self):
        return self.type is not None and self.count > 0 and not self.asset

    def num_children(self):
        return min(self.count, 10000) if self.has_children() else 0

    def get_child_index(self, name):
        try:
            return int(name.strip("[]"))
        except ValueError:
            return -1

    def get_child_at_index(self, index):
        address = _element(self.valobj, self.at, self.block, index, self.kind[2])
        if address is None:
            return None
        return self.valobj.CreateValueFromAddress("[%d]" % index, address, self.type)


class TideLocalList(TideList):
    """A list local or parameter of a debug build: its List<T>'s struct of its own, the list in `list`."""

    def _list(self):
        return self.valobj.GetNonSyntheticValue().GetChildMemberWithName("list")

    def _kind(self):
        return TIDE_LOCAL_LISTS.get(self.valobj.GetType().GetUnqualifiedType().GetName())


def local_list_summary(valobj, internal_dict):
    return list_summary(valobj.GetNonSyntheticValue().GetChildMemberWithName("list"), internal_dict)


def list_summary(valobj, internal_dict):
    at = _handle(valobj.GetNonSyntheticValue())
    if not at:
        return "count = 0"
    block = _block(valobj, at)
    if block is None:
        return "<a list in a heap this thread can't see>"
    count, b, _ = _block_counts(valobj, block)
    return "count = %d%s" % (count, ", an asset's" if b == TIDE_LIST_ASSET else "")


class TideMap:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.update()

    def update(self):
        self.entries = []
        kind = _kind_of(self.valobj)
        if not kind or kind[0] != "map":
            return False
        target = self.valobj.GetTarget()
        entry_type = target.FindFirstType(kind[1])
        lists = self.valobj.GetChildMemberWithName("entries")
        at = _handle(lists)
        block = _block(self.valobj, at)
        if block is None or not entry_type.IsValid():
            return False
        count, _, _ = _block_counts(self.valobj, block)
        process = self.valobj.GetProcess()
        for i in range(min(count or 0, 10000)):
            address = _element(self.valobj, at, block, i, kind[2])
            if address is None:
                break
            order = _read(process, address, 8)
            if order is None or order == 0 or order >= 1 << 63:
                continue  # Removed: its order negated, or nothing there
            entry = self.valobj.CreateValueFromAddress("entry", address, entry_type)
            key = entry.GetChildMemberWithName("key")
            if kind[3]:
                self.entries.append(("[%d]" % len(self.entries), key))
            else:
                value = entry.GetChildMemberWithName("value")
                shown = key.GetSummary() or key.GetValue() or "?"
                self.entries.append(("[%s]" % shown, value))
        return False

    def has_children(self):
        return bool(self.entries)

    def num_children(self):
        return len(self.entries)

    def get_child_index(self, name):
        for i, (shown, _) in enumerate(self.entries):
            if shown == name:
                return i
        return -1

    def get_child_at_index(self, index):
        name, value = self.entries[index]
        return value.CreateValueFromAddress(name, value.GetLoadAddress(), value.GetType())


def map_summary(valobj, internal_dict):
    return "count = %d" % valobj.GetNonSyntheticValue().GetChildMemberWithName("live").GetValueAsUnsigned()


def enum_summary(valobj, internal_dict):
    names = TIDE_ENUMS.get(valobj.GetTypeName(), {})
    number = valobj.GetValueAsSigned()
    return names.get(number, str(number))


class TideStruct:
    """A game type with lists, maps or enums: its fields by their Tide names, containers typed."""

    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.update()

    def update(self):
        self.fields = TIDE_TYPES.get(self.valobj.GetType().GetUnqualifiedType().GetName(), [])
        return False

    def has_children(self):
        return True

    def num_children(self):
        return len(self.fields)

    def get_child_index(self, name):
        for i, field in enumerate(self.fields):
            if field[0] == name:
                return i
        return -1

    def get_child_at_index(self, index):
        name, cname, kind = self.fields[index]
        child = self.valobj.GetNonSyntheticValue().GetChildMemberWithName(cname)
        if not child.IsValid():
            return None
        address = child.GetLoadAddress()
        if kind and kind[0] in ("list", "map") and address != lldb.LLDB_INVALID_ADDRESS:
            _registered[address] = kind
        if address == lldb.LLDB_INVALID_ADDRESS or name == cname:
            return child
        return self.valobj.CreateValueFromAddress(name, address, child.GetType())


class TideHidden:
    """No children: the engine's bookkeeping beside a function's code."""

    def __init__(self, valobj, internal_dict):
        pass

    def num_children(self):
        return 0

    def has_children(self):
        return False

    def get_child_index(self, name):
        return -1

    def get_child_at_index(self, index):
        return None

    def update(self):
        return False


def _not_starting_with(words):
    """An extended regular expression for any text that starts with none of `words`."""
    alternatives = ["$"]
    firsts = sorted(set(w[0] for w in words if w))
    if not firsts:
        return ".*"
    alternatives.append("[^" + "".join(firsts) + "]")
    for c in firsts:
        rest = [w[1:] for w in words if w and w[0] == c]
        if "" in rest:
            continue  # A word ends here: whatever follows starts with it
        alternatives.append(c + "(" + _not_starting_with(rest) + ")")
    return "(" + "|".join(alternatives) + ")"


# Functions of the game's own code (codegen.c's names), which stepping goes into:
# everything else named tide_ is the engine's, which it steps over.
TIDE_CODE_PREFIXES = ["system_", "view_", "handler_", "function_", "method_", "property_", "operator_"]


def tide_register(debugger, module):
    def run(command):
        debugger.HandleCommand(command)

    run("type category define tide")
    run("type summary add -w tide -F %s.entity_summary tide_entity" % module)
    run("type summary add -w tide -F %s.str_summary tide_str" % module)
    run("type summary add -w tide -F %s.text_summary tide_text" % module)
    run("type synthetic add -w tide -l %s.TideList tide_list" % module)
    run("type summary add -w tide -e -F %s.list_summary tide_list" % module)
    run("type synthetic add -w tide -l %s.TideMap tide_map" % module)
    run("type summary add -w tide -e -F %s.map_summary tide_map" % module)
    for name, shown in (("tide_float2", "(${var.x}, ${var.y})"), ("tide_float3", "(${var.x}, ${var.y}, ${var.z})"),
                        ("tide_float4", "(${var.x}, ${var.y}, ${var.z}, ${var.w})"), ("tide_int2", "(${var.x}, ${var.y})"),
                        ("tide_int3", "(${var.x}, ${var.y}, ${var.z})"), ("tide_int4", "(${var.x}, ${var.y}, ${var.z}, ${var.w})"),
                        ("tide_double2", "(${var.x}, ${var.y})"), ("tide_double3", "(${var.x}, ${var.y}, ${var.z})"),
                        ("tide_double4", "(${var.x}, ${var.y}, ${var.z}, ${var.w})"),
                        ("tide_quaternion", "(${var.value.x}, ${var.value.y}, ${var.value.z}, ${var.value.w})"),
                        ("tide_color", "Color(${var.r}, ${var.g}, ${var.b}, ${var.a})")):
        run("type summary add -w tide -s \"%s\" %s" % (shown, name))
    # What generated code keeps a function's list and map lookups in: the engine's, nothing of the game's
    hidden = "'^tide_(list_cache|map_view)(\\[[0-9]+\\])?$'"
    run("type synthetic add -w tide -x -l %s.TideHidden %s" % (module, hidden))
    run("type summary add -w tide -x -e -s \"the engine's\" %s" % hidden)
    for name in TIDE_LOCAL_LISTS:
        run("type synthetic add -w tide -l %s.TideLocalList %s" % (module, name))
        run("type summary add -w tide -e -F %s.local_list_summary %s" % (module, name))
    for name in TIDE_TYPES:
        run("type synthetic add -w tide -l %s.TideStruct %s" % (module, name))
    for name in TIDE_ENUMS:
        run("type summary add -w tide -v -F %s.enum_summary %s" % (module, name))
    run("type category enable tide")
    run("settings set target.process.thread.step-avoid-regexp '^tide_%s'" % _not_starting_with(TIDE_CODE_PREFIXES))
