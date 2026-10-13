import secrets
import pytest
from pymysql.constants import CLIENT
import pymysql
from harness.cluster import ROOT
from harness.checks import require

pytestmark = [pytest.mark.integration, pytest.mark.feature("DB-MIGRATION")]


@pytest.mark.parametrize("old_users", [False, True])
def test_initializer_preserves_data_and_applicable_migrations_are_repeatable(cluster, old_users):
    schema = "compat_" + secrets.token_hex(8)
    connection = pymysql.connect(
        host=cluster.mysql_host,
        port=cluster.mysql_port,
        user="root",
        password=cluster.root_password,
        autocommit=True,
        client_flag=CLIENT.MULTI_STATEMENTS,
    )
    with connection:
        with connection.cursor() as cursor:
            try:
                cursor.execute(f"CREATE DATABASE `{schema}` CHARACTER SET utf8mb4")
                cursor.execute(f"USE `{schema}`")
                if old_users:
                    cursor.execute(
                        "CREATE TABLE users(id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY, username VARCHAR(64), email VARCHAR(255), password_hash VARCHAR(255), password_salt VARCHAR(64), password_iterations INT)"
                    )
                    cursor.execute(
                        "INSERT INTO users(username,email) VALUES('Existing','old@example.test')"
                    )
                initializer = (
                    (ROOT / "database/mysql/init_cppchat.sql")
                    .read_text(encoding="utf-8-sig")
                    .replace("`cppchat`", f"`{schema}`")
                )
                for _ in range(2):
                    cursor.execute(initializer)
                    while cursor.nextset():
                        pass
                    for file in cluster.migrations[1:]:
                        cursor.execute(file.read_text(encoding="utf-8-sig"))
                        while cursor.nextset():
                            pass
                cursor.execute(
                    "SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=%s AND TABLE_NAME='users'",
                    (schema,),
                )
                columns = {row[0] for row in cursor.fetchall()}
                require(
                    {"nick", "sex", "profile_description", "icon"} <= columns,
                    "Old profile columns not migrated",
                )
                if old_users:
                    cursor.execute("SELECT COUNT(*) FROM users WHERE username='Existing'")
                    require(cursor.fetchone()[0] == 1, "Migration lost existing user")
                cursor.execute(
                    "SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=%s AND TABLE_NAME='chat_message'",
                    (schema,),
                )
                require({"msg_type", "media_size"} <= {row[0] for row in cursor.fetchall()})
            finally:
                cursor.execute(f"DROP DATABASE IF EXISTS `{schema}`")
