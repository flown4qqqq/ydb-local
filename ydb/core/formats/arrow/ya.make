RECURSE_FOR_TESTS(
    ut
)

LIBRARY()

PEERDIR(
    contrib/libs/apache/arrow
    ydb/core/scheme
    ydb/core/formats/arrow/accessor
    ydb/core/formats/arrow/serializer
    ydb/core/formats/arrow/dictionary
    ydb/core/formats/arrow/transformer
    ydb/core/formats/arrow/reader
    ydb/core/formats/arrow/save_load
    ydb/core/formats/arrow/splitter
    ydb/core/formats/arrow/hash
    ydb/library/actors/core
    ydb/library/arrow_kernels
    yql/essentials/types/binary_json
    yql/essentials/types/dynumber
    ydb/library/formats/arrow
    ydb/library/services
    yql/essentials/core/arrow_kernels/request
)

IF (OS_WINDOWS)
    ADDINCL(
        ydb/library/yql/udfs/common/clickhouse/client/base
        ydb/library/arrow_clickhouse
    )
ELSE()
    PEERDIR(
        ydb/library/arrow_clickhouse
    )
    ADDINCL(
        ydb/library/arrow_clickhouse
    )
ENDIF()

YQL_LAST_ABI_VERSION()

SRCS(
    arrow_batch_builder.cpp
    arrow_filter.cpp
    arrow_helpers.cpp
    converter.cpp
    converter.h
    custom_registry.cpp
    permutations.cpp
    program.cpp
    size_calcer.cpp
    ssa_program_optimizer.cpp
    special_keys.cpp
    process_columns.cpp
)

END()
