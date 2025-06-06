GTEST(unittester-core-ytree)

INCLUDE(${ARCADIA_ROOT}/yt/ya_cpp.make.inc)

PROTO_NAMESPACE(yt)

SRCS(
    attributes_ut.cpp
    attribute_filter_ut.cpp
    resolver_ut.cpp
    serialize_ut.cpp
    service_combiner_ut.cpp
    size_ut.cpp
    text_yson_convert_ut.cpp
    tree_builder_ut.cpp
    lazy_ypath_service_ut.cpp
    yson_schema_ut.cpp
    yson_struct_update_ut.cpp
    yson_struct_ut.cpp
    ytree_fluent_ut.cpp
    ytree_ut.cpp

    proto/test.proto
)

GENERATE_ENUM_SERIALIZATION(serialize_ut.h)

INCLUDE(${ARCADIA_ROOT}/yt/opensource.inc)

PEERDIR(
    yt/yt/core
    yt/yt/core/test_framework
)

REQUIREMENTS(
    cpu:4
    ram:4
    ram_disk:1
)

FORK_TESTS()

SPLIT_FACTOR(5)

SIZE(SMALL)

IF (OS_DARWIN)
    SIZE(LARGE)
    TAG(
        ya:fat
        ya:force_sandbox
        ya:exotic_platform
        ya:large_tests_on_single_slots
    )
ENDIF()

END()
