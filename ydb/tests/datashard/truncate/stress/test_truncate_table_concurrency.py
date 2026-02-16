# -*- coding: utf-8 -*-
import ydb
import pytest
import random
from ydb.tests.sql.lib.test_base import TestBase


class TestTruncateTableConcurrency(TestBase):
    
    def test_truncate_with_concurrent_rw_operations(self):
        table_name = f"{self.table_path}_truncate_concurrency"
        
        self._create_test_table(table_name)
        self._initialize_table_data(table_name)
    
    def _create_test_table(self, table_name : str):
        self.query(
            f"""
            CREATE TABLE `{table_name}` (
                id Int64 NOT NULL,
                numeric_value_1 Int64 NOT NULL,
                numeric_value_2 Double,
                vector_data String NOT NULL,
                text_data Utf8 NOT NULL,
                PRIMARY KEY (id)
            )
            """
        )
    
    def _initialize_table_data(self, table_name : str):
        sql_upsert = f"""
            UPSERT INTO `{table_name}` (id, numeric_value_1, numeric_value_2, vector_data, text_data)
            VALUES
        """

        rows_count = 10

        for i in range(rows_count):
            numeric_1 = i * 100 + random.randint(1, 99)
            numeric_2 = round(random.uniform(1.0, 1000.0), 2)

            vector_data = f"[{random.uniform(0,1):.3f},{random.uniform(0,1):.3f},{random.uniform(0,1):.3f}]"
            text_data = f"test_string_{i}_{random.randint(1000, 9999)}"
            sql_upsert += f"({i}, {numeric_1}, {numeric_2}, '{vector_data}', '{text_data}')"

            if i + 1 == rows_count:
                sql_upsert += ";"
            else:
                sql_upsert += ",\n"

        self.query(sql_upsert)

        self._verify_table_count(table_name, rows_count)

    def _verify_table_count(self, table_name : str, expected_count : int):
        result = self.query(f"SELECT COUNT(*) as row_count FROM `{table_name}`")
        assert len(result) > 0, "Expected count result"
        
        row_count = result[0]["row_count"]
        assert row_count == expected_count, f"Expected {expected_count} rows, got {row_count}"
