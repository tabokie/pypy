
from rpython.rtyper.lltypesystem import rffi, lltype
from rpython.jit.backend.llsupport.codemap import stack_depth_at_loc
from rpython.jit.backend.llsupport.codemap import CodemapStorage, \
     CodemapBuilder, unpack_traceback, find_codemap_at_addr

NULL = lltype.nullptr(rffi.CArray(lltype.Signed))
     
def test_register_codemap():
    codemap = CodemapStorage()
    codemap.setup()
    codemap.register_codemap((100, 20, [13, 14, 15]))
    codemap.register_codemap((300, 30, [16, 17, 18]))
    codemap.register_codemap((200, 100, [19, 20, 21, 22, 23]))
    #
    raw100 = find_codemap_at_addr(100, NULL)
    assert find_codemap_at_addr(119, NULL) == raw100
    assert not find_codemap_at_addr(120, NULL)
    #
    raw200 = find_codemap_at_addr(200, NULL)
    assert raw200 != raw100
    assert find_codemap_at_addr(299, NULL) == raw200
    #
    raw300 = find_codemap_at_addr(329, NULL)
    assert raw300 != raw100 and raw300 != raw200
    assert find_codemap_at_addr(300, NULL) == raw300
    #
    codemap.free()

def test_find_jit_frame_depth():
    codemap = CodemapStorage()
    codemap.setup()
    codemap.register_frame_depth_map(11, 26, [0, 5, 10], [1, 2, 3])
    codemap.register_frame_depth_map(30, 41, [0, 5, 10], [4, 5, 6])
    codemap.register_frame_depth_map(0, 11, [0, 5, 10], [7, 8, 9])
    assert stack_depth_at_loc(13) == 1
    assert stack_depth_at_loc(-3) == -1
    assert stack_depth_at_loc(40) == 6
    assert stack_depth_at_loc(41) == -1
    assert stack_depth_at_loc(5) == 8
    assert stack_depth_at_loc(17) == 2
    assert stack_depth_at_loc(38) == 5
    assert stack_depth_at_loc(25) == 3
    assert stack_depth_at_loc(26) == -1
    assert stack_depth_at_loc(11) == 1
    assert stack_depth_at_loc(10) == 9
    codemap.free_asm_block(11, 26)
    assert stack_depth_at_loc(11) == -1
    assert stack_depth_at_loc(13) == -1
    assert stack_depth_at_loc(-3) == -1
    assert stack_depth_at_loc(40) == 6
    assert stack_depth_at_loc(41) == -1
    assert stack_depth_at_loc(5) == 8
    assert stack_depth_at_loc(38) == 5
    assert stack_depth_at_loc(10) == 9
    codemap.free()

def test_codemaps():
    builder = CodemapBuilder()
    builder.debug_merge_point(0, 102, 0)
    builder.debug_merge_point(0, 102, 13)
    builder.debug_merge_point(1, 104, 15)
    builder.debug_merge_point(1, 104, 16)
    builder.debug_merge_point(2, 106, 20)
    builder.debug_merge_point(2, 106, 25)
    builder.debug_merge_point(1, 104, 30)
    builder.debug_merge_point(0, 102, 35)
    codemap = CodemapStorage()
    codemap.setup()
    codemap.register_codemap(builder.get_final_bytecode(100, 40))
    builder = CodemapBuilder()
    builder.debug_merge_point(0, 202, 0)
    builder.debug_merge_point(0, 202, 10)
    builder.debug_merge_point(1, 204, 20)
    builder.debug_merge_point(1, 204, 30)
    builder.debug_merge_point(2, 206, 40)
    builder.debug_merge_point(2, 206, 50)
    builder.debug_merge_point(1, 204, 60)
    builder.debug_merge_point(0, 202, 70)
    codemap.register_codemap(builder.get_final_bytecode(200, 100))
    assert unpack_traceback(110) == [102]
    assert unpack_traceback(117) == [102, 104]
    assert unpack_traceback(121) == [102, 104, 106]
    assert unpack_traceback(131) == [102, 104]
    assert unpack_traceback(137) == [102]
    assert unpack_traceback(205) == [202]
    assert unpack_traceback(225) == [202, 204]
    assert unpack_traceback(245) == [202, 204, 206]
    assert unpack_traceback(265) == [202, 204]
    assert unpack_traceback(275) == [202]
    codemap.free_asm_block(200, 300)
    assert unpack_traceback(225) == []
    codemap.free()