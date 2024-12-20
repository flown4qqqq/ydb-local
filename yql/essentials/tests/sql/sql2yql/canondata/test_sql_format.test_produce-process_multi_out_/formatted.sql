/* syntax version 1 */
/* postgres can not */
$udfScript = @@
def MyFunc(list):
    return [(int(x.key) % 2, x) for x in list]
@@;

$record = (
    SELECT
        TableRow()
    FROM
        plato.Input
);

$recordType = TypeOf(Unwrap($record));

$udf = Python::MyFunc(
    CallableType(
        0,
        StreamType(
            VariantType(TupleType($recordType, $recordType))
        ),
        StreamType($recordType)
    ),
    $udfScript
);

$i, $j = (
    PROCESS plato.Input
    USING $udf(TableRows())
);

SELECT
    *
FROM
    $i
;

SELECT
    *
FROM
    $j
;
