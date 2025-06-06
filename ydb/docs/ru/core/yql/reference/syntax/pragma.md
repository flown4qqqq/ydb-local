# PRAGMA

## Определение

Переопределение настроек.

### Синтаксис

`PRAGMA x.y = "z";` или `PRAGMA x.y("z", "z2", "z3");`:

* `x` — (опционально) категория настройки.
* `y` — название настройки.
* `z` — (опционально для флагов) значение настройки. Допустимо использование следующих суффиксов:

  * `Kb`, `Mb`, `Gb` — для объема информации.
  * `sec`, `min`, `h`, `d` — для временных значений.

### Примеры

```yql
PRAGMA AutoCommit;
```

```yql
PRAGMA TablePathPrefix = "home/yql";
```

```yql
PRAGMA Warning("disable", "1101");
```

За некоторым исключением, значение настроек можно вернуть в состояние по умолчанию с помощью `PRAGMA my_pragma = default;`.

Полный список доступных настроек [см. в таблице ниже](#pragmas).

### Область действия {#pragmascope}

Если не указано иное, прагма влияет на все идущие следом выражения вплоть до конца модуля, в котором она встречается.
При необходимости и логической возможности допустимо менять значение настройки несколько раз в одном запросе, чтобы оно было разным на разных этапах выполнения.
Существуют также специальные scoped прагмы, область действия которых определяется по тем же правилам, что и область видимости [именованных выражений](expressions.md#named-nodes).
В отличие от scoped прагм, обычные прагмы могут использоваться только в глобальной области видимости (не внутри лямбда-функций, ACTION{% if feature_subquery %}, SUBQUERY{% endif %} и т.п.).

## Глобальные {#pragmas}

### AutoCommit {#autocommit}

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Автоматически выполнять [COMMIT](commit.md) после каждого выражения.

### TablePathPrefix {#table-path-prefix}

| Тип значения | По умолчанию |
| --- | --- |
| Строка | — |

Добавить указанный префикс к путям таблиц внутри кластеров. Работает по принципу объединения путей в файловой системе: поддерживает ссылки на родительский каталог `..` и не требует добавления слеша справа. Например,

```yql
PRAGMA TablePathPrefix = "home/yql";
SELECT * FROM test;
```

Префикс не добавляется, если имя таблицы указано как абсолютный путь (начинается с /).

### UDF {#udf}

| Тип значения | По умолчанию | Статическая /<br/>динамическая |
| --- | --- | --- |
| Строка | — | Статическая |
| Строка - имя префикса, добавляемого ко всем модулям | "" | Статическая |

Импорт всех UDF из указанной приложенной к запросу скомпилированной под Linux x64 разделяемой библиотеки (.so).
При указании префикса, он добавляется перед названием всех загруженных модулей, например, CustomPrefixIp::IsIPv4 вместо Ip::IsIPv4.
Указание префикса позволяет подгрузить одну и ту же UDF разных версий.

### UseTablePrefixForEach {#use-table-prefix-for-each}

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

`EACH` использует [TablePathPrefix](#table-path-prefix) для каждого элемента списка.

### Warning {#warning}

| Тип значения | По умолчанию |
| --- | --- |
| 1. Действие<br/>2. Код предупреждения либо символ "*"| — |

Действие:

* `disable` — отключить;
* `error` — приравнять к ошибке;
* `default` — вернуть поведение по умолчанию.

Код предупреждения возвращается вместе с самим текстом (в веб-интерфейсе отображается справа).

Примеры:

`PRAGMA Warning("error", "*");`
`PRAGMA Warning("disable", "1101");`
`PRAGMA Warning("default", "4503");`

В данном случае все предупреждения будут считаться ошибками, за исключением предупреждение с кодом `1101`, которое будет отключено, и `4503`, которое будет обрабатываться по умолчанию (то есть останется предупреждением). Поскольку предупреждения могут добавляться в новых релизах YQL, следует с осторожностью пользоваться конструкцией `PRAGMA Warning("error", "*");` (как минимум покрывать такие запросы автотестами).



### Greetings {#greetings}

| Тип значения | По умолчанию |
| --- | --- |
| Текст | — |

Выдать указанный текст в качестве Info сообщения запроса.

Пример:
`PRAGMA Greetings("It's a good day!");`

### WarningMsg {#warningmsg}

| Тип значения | По умолчанию |
| --- | --- |
| Текст | — |

Выдать указанный текст в качестве Warning сообщения запроса.

Пример:
`PRAGMA WarningMsg("Attention!");`

{% if feature_mapreduce %}

### DqEngine {#dqengine}

| Тип значения | По умолчанию |
| --- | --- |
| Строка disable/auto/force | "auto" |

При значении "auto" включает новый движок вычислений. Вычисления по возможности делаются без создания map/reduce операций. При значении "force" вычисление идет в новый движок безусловно.
{% endif %}

{% if feature_join %}

### SimpleColumns {#simplecolumns}

`SimpleColumns` / `DisableSimpleColumns`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | true |

При использовании `SELECT foo.* FROM ... AS foo` убрать префикс  `foo.` у имен результирующих колонок.

Работает в том числе и для [JOIN](select/join.md), но в этом случае имеет право упасть в случае конфликта имен (который можно разрешить с помощью [WITHOUT](select/without.md) и переименования колонок). Для JOIN в режиме SimpleColumns производится неявный Coalesce ключевых колонок: запрос `SELECT * FROM T1 AS a JOIN T2 AS b USING(key)` в режиме SimpleColumns работает аналогично `SELECT a.key ?? b.key AS key, ... FROM T1 AS a JOIN T2 AS b USING(key)`.

### CoalesceJoinKeysOnQualifiedAll

`CoalesceJoinKeysOnQualifiedAll` / `DisableCoalesceJoinKeysOnQualifiedAll`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | true |

Управляет неявным Coalesce для ключевых колонок `JOIN` в режиме SimpleColumns. Если флаг установлен, то Coalesce ключевых колонок происходит при наличии хотя бы одного выражения вида `foo.*` или `*` в SELECT - например `SELECT a.* FROM T1 AS a JOIN T2 AS b USING(key)`. Если флаг сброшен, то Coalesce ключей JOIN происходит только при наличии '*' после `SELECT`

### StrictJoinKeyTypes

`StrictJoinKeyTypes` / `DisableStrictJoinKeyTypes`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Если флаг установлен, то [JOIN](select/join.md) будет требовать строгого совпадения типов ключей.
По умолчанию JOIN предварительно конвертирует ключи к общему типу, что может быть нежелательно с точки зрения производительности.
StrictJoinKeyTypes является [scoped](#pragmascope) настройкой.

{% endif %}

### AnsiInForEmptyOrNullableItemsCollections

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Наличие этой прагмы приводит поведение оператора `IN` в соответствие со стандартом в случае наличия `NULL` в левой или правой части оператора `IN`. Также изменено поведение `IN` в случае, когда справа был Tuple с элементами разных типов. Примеры:

`1 IN (2, 3, NULL) = NULL (было Just(False))`
`NULL IN () = Just(False) (было NULL)`
`(1, null) IN ((2, 2), (3, 3)) = Just(False) (было NULL)`

Подробнее про поведение `IN` при наличии `NULL`ов в операндах можно почитать [здесь](expressions.md#in). Явным образом выбрать старое поведение можно указав прагму `DisableAnsiInForEmptyOrNullableItemsCollections`. Если никакой прагмы не задано, то выдается предупреждение и работает старый вариант.

### AnsiRankForNullableKeys

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Приводит поведение RANK/DENSE_RANK в соответствие со стандартом при наличии опциональных типов в ключах сортировки окна или в аргументе этих оконных функций. А именно:

* типом результата всегда является Uint64, а не Uint64?;
* null-ы в ключах считаются равными друг другу (текущая реализация возвращает NULL).

Явным образом выбрать старое поведению можно указав прагму `DisableAnsiRankForNullableKeys`. Если никакой прагмы не задано, то выдается предупреждение и работает старый вариант.

### AnsiCurrentRow

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Приводит неявное задание рамки окна при наличии ORDER BY в соответствие со стандартом.
Если AnsiCurrentRow не установлен, то окно `(ORDER BY key)` эквивалентно `(ORDER BY key ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)`.
Стандарт же требует, чтобы такое окно вело себя как `(ORDER BY key RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)`.
Разница состоит в трактовке `CURRENT ROW`. В режиме `ROWS` `CURRENT ROW` трактуется буквально – текущая строка в партиции.
А в режиме `RANGE` конец рамки `CURRENT ROW` означает "последняя строка в партиции с ключом сортировки, равным текущей строке".

### OrderedColumns {#orderedcolumns}

`OrderedColumns` / `DisableOrderedColumns`

Выводить [порядок колонок](select/index.md#orderedcolumns) в SELECT/JOIN/UNION ALL и сохранять его при записи результатов. По умолчанию порядок колонок не определен.

### PositionalUnionAll {#positionalunionall}

Включить соответствующий стандарту поколоночный режим выполнения [UNION ALL](select/union.md#union-all). При этом автоматически включается
[упорядоченность колонок](#orderedcolumns).

### RegexUseRe2

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Использовать Re2 UDF вместо Pcre для выполнения SQL операторов `REGEX`,`MATCH`,`RLIKE`. Re2 UDF поддерживает корректную обработку Unicode-символов в отличие от используемой по умолчанию Pcre UDF.

### ClassicDivision

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | true |

В классическом варианте результат целочисленного деления остаётся целочисленным (по умолчанию).
Если отключить — результат всегда становится Double.
ClassicDivision является [scoped](#pragmascope) настройкой.

### CheckedOps

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

При включенном режиме если в результате выполнения агрегационных функций SUM/SUM_IF, бинарных операций `+`,`-`,`*`,`/`,`%` или унарной операции `-` над целыми числами происходит выход за границы целевого типа аргументов или результата, то возвращается `NULL`.
Если отключить - переполнение не проверяется.
Не влияет на операции с числами с  плавающей точкой или `Decimal`.
CheckedOps является [scoped](#pragmascope) настройкой.

### UnicodeLiterals

`UnicodeLiterals`/`DisableUnicodeLiterals`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

При включенном режиме строковые литералы без суффиксов вида "foo"/'bar'/@@multiline@@ будут иметь тип `Utf8`, при выключенном - `String`.
UnicodeLiterals является [scoped](#pragmascope) настройкой.

### WarnUntypedStringLiterals

`WarnUntypedStringLiterals`/`DisableWarnUntypedStringLiterals`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

При включенном режиме для строковых литералов без суффиксов вида "foo"/'bar'/@@multiline@@ будет генерироваться предупреждение. Его можно подавить, если явно выбрать суффикс `s` для типа `String`, либо `u` для типа `Utf8`.
WarnUntypedStringLiterals является [scoped](#pragmascope) настройкой.

### AllowDotInAlias

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Разрешить использовать точку в именах результирующих колонок. По умолчанию отключено, т.к. дальнейшее использование таких колонок в JOIN полностью не реализовано.

### WarnUnnamedColumns

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Генерировать предупреждение если для безымянного выражения в `SELECT` было автоматически сгенерировано имя колонки (вида `column[0-9]+`).

### GroupByLimit

| Тип значения | По умолчанию |
| --- | --- |
| Положительное число | 32 |

Увеличение лимита на число  группировок в [GROUP BY](select/group-by.md).

{% if feature_group_by_rollup_cube %}

### GroupByCubeLimit

| Тип значения | По умолчанию |
| --- | --- |
| Положительное число | 5 |

Увеличение лимита на число размерностей [GROUP BY](select/group-by.md#rollup-cube-group-sets).

Использовать нужно аккуратно, так как вычислительная сложность запроса растет экспоненциально по отношению к числу размерностей.

{% endif %}


## Yson

Управление поведением Yson UDF по умолчанию, подробнее см. в [документации по ней](../udf/list/yson.md) и в частности [Yson::Options](../udf/list/yson.md#ysonoptions).

### `yson.AutoConvert`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Автоматическое конвертация значений в требуемый тип данных во всех вызовах Yson UDF, в том числе и неявных.

### `yson.Strict`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | true |

Управление строгим режимом во всех вызовах Yson UDF, в том числе и неявных. Без значения или при значении `"true"` - включает строгий режим. Со значением `"false"` - отключает.

### `yson.DisableStrict`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Инвертированная версия `yson.Strict`. Без значения или при значении `"true"` - отключает строгий режим. Со значением `"false"` - включает.



{% if feature_mapreduce %}

## Работа с файлами

### File

| Тип значения | По умолчанию | Статическая /<br/>динамическая |
| --- | --- | --- |
| Два или три строковых аргумента — алиас, URL и опциональное имя токена | — | Статическая |

Приложить файл к запросу по URL. Использовать приложенные файлы можно с помощью встроенных функций [FilePath и FileContent](../builtins/basic.md#filecontent).{% if oss != true %} Данная `PRAGMA` является универсальной альтернативой прикладыванию файлов с использованием встроенных механизмов [веб-](../interfaces/web.md#attach) или [консольного](../interfaces/cli.md#attach) клиентов.{% endif %}

Сервис YQL оставляет за собой право кешировать находящиеся за URL файлы на неопределенный срок, по-этому при значимом изменении находящегося за ней содержимого настоятельно рекомендуется модифицировать URL за счет добавления/изменения незначащих параметров.

При указании имени токена, его значение будет использоваться для обращения к целевой системе.

### FileOption

| Тип значения                                    | По умолчанию | Статическая /<br/> динамическая |
|-------------------------------------------------|--------------|--------------------------------|
| Три строковых аргумента — алиас, ключ, значение | —            | Статическая                    |

Установить у указанного файла опцию по заданному ключу в заданное значение. Файл с этим алиасом уже должен быть объявлен
через [PRAGMA File](#file) или приложен к запросу.



### Folder

| Тип значения | По умолчанию | Статическая /<br/>динамическая |
| --- | --- | --- |
| Два или три строковых аргумента — префикс, URL и опциональное имя токена | — | Статическая |

Приложить набор файлов к запросу по URL. Работает аналогично добавлению множества файлов через [PRAGMA File](#file) по прямым ссылкам на файлы с алиасами, полученными объединением префикса с именем файла через `/`.

При указании имени токена, его значение будет использоваться для обращения к целевой системе.

### Library

| Тип значения | По умолчанию | Статическая /<br/>динамическая |
| --- | --- | --- |
| Один или два аргумента - имя файла и опциональный URL | — | Статическая |

Интерпретировать указанный приложенный файл как библиотеку, из которой можно делать [IMPORT](export_import.md). Тип синтаксиса библиотеки определяется по расширению файла:
* `.sql` для YQL диалекта SQL <span style="color: green;">(рекомендуется)</span>;
* `.yql` для [s-expressions](/docs/s_expressions).

Пример с приложенным файлом к запросу:

```yql
PRAGMA library("a.sql");
IMPORT a SYMBOLS $x;
SELECT $x;
```

В случае указания URL библиотека скачивается с него, а не с предварительного приложенного файла, как в следующем примере:

```yql
PRAGMA library("a.sql","{{ corporate-paste }}/5618566/text");
IMPORT a SYMBOLS $x;
SELECT $x;
```

При этом можно использовать подстановку текстовых параметров в URL:

```yql
DECLARE $_ver AS STRING; -- "5618566"
PRAGMA library("a.sql","{{ corporate-paste }}/{$_ver}/text");
IMPORT a SYMBOLS $x;
SELECT $x;
```

### Package

| Тип значения | По умолчанию | Статическая /<br/>динамическая |
| --- | --- | --- |
| Два или три аргумента - имя пакета, URL и опциональный токен | — | Статическая |

Приложить иерархический набор файлов к запросу по URL, интерпретируя их в качестве пакета с указанным именем - взаимосвязанного набора библиотек.

Имя пакета ожидается в формате ``project_name.package_name``; из библиотек пакета в дальнейшнем можно делать [IMPORT](export_import.md) с именем модуля вида ``pkg.project_name.package_name.maybe.nested.module.name``.

Пример для пакета с плоской иерархией, состоящего из двух библиотек - foo.sql и bar.sql:

```yql
PRAGMA package("project.package", "{{ corporate-yt }}/{{ corporate-yt-cluster }}/path/to/package");
IMPORT pkg.project.package.foo SYMBOLS $foo;
IMPORT pkg.project.package.bar SYMBOLS $bar;
SELECT $foo, $bar;
```

При этом можно использовать подстановку текстовых параметров в URL:

```yql
DECLARE $_path AS STRING; -- "path"
PRAGMA package("project.package","{{ corporate-yt }}/{{ corporate-yt-cluster }}/{$_path}/to/package");
IMPORT pkg.project.package.foo SYMBOLS $foo;
IMPORT pkg.project.package.bar SYMBOLS $bar;
SELECT $foo, $bar;
```

### OverrideLibrary

| Тип значения | По умолчанию | Статическая /<br/>динамическая |
| --- | --- | --- |
| Один аргумент - имя файла | — | Статическая |

Интерпретировать указанный приложенный файл как библиотеку и перекрыть ей одну из библиотек пакета.

Имя файла ожидается в формате ``project_name/package_name/maybe/nested/module/name.EXTENSION``, поддерживаются аналогичные [PRAGMA Library](#library) расширения.

Пример:

```yql
PRAGMA package("project.package", "{{ corporate-yt }}/{{ corporate-yt-cluster }}/path/to/package");
PRAGMA override_library("project/package/maybe/nested/module/name.sql");

IMPORT pkg.project.package.foo SYMBOLS $foo;
SELECT $foo;
```

{% endif %}


{% if backend_name == "YDB" %}

## YDB

### `ydb.CostBasedOptimizationLevel` {#costbasedoptimizationlevel}

| Уровень | Поведение оптимизатора |
| ------- | ---------------------- |
| 0 | Cтоимостный оптимизатор выключен |
| 1 | Cтоимостный оптимизатор выключен, считаются предсказания оптимизатора |
| 2 | Cтоимостный оптимизатор включается только для запросов, где участвуют {% if backend_name == "YDB" and oss == true %}[колоночные таблицы](../../../concepts/glossary.md#column-oriented-table){% else %}колоночные таблицы{% endif %} |
| 3 | Cтоимостный оптимизатор включается для всех запросов, но для строковых таблиц предпочитается алгоритм джоина LookupJoin |
| 4 | Cтоимостный оптимизатор включен для всех запросов |

{% if tech %}

### `kikimr.IsolationLevel`

| Тип значения | По умолчанию |
| --- | --- |
| Serializable, ReadCommitted, ReadUncommitted или ReadStale. | Serializable |

Экспериментальная pragma, позволяет ослабить уровень изоляции текущей транзакции в YDB.

{% endif %}


{% endif %}

{% if tech %}

## Отладочные и служебные {#debug}

{% if feature_webui %}

### `DirectRead`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Служебная настройка для работы preview таблиц в [HTTP API](../interfaces/http.md) (для веб-интерфейса и консольного клиента).
{% endif %}

### `config.flags("ValidateUdf", "Lazy")`

| Тип значения | По умолчанию |
| --- | --- |
| Строка: None / Lazy / Greedy | None |

Валидация результатов UDF на соответствие объявленной сигнатуре. Greedy режим форсирует материализацию «ленивых» контейнеров, а Lazy — нет.

### `{{ backend_name_lower }}.DefaultCluster`

| Тип значения | По умолчанию |
| --- | --- |
| Строка с именем кластера | hahn |

Выбор кластера для выполнения вычислений, не использующих таблицы.

### `config.flags("Diagnostics")`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Получение диагностической информации от YQL в виде дополнительного результата запроса.

{% endif %}
