import secrets
import pytest
from pymysql.constants import CLIENT
import pymysql
from harness.cluster import ROOT
from harness.checks import require

pytestmark = [pytest.mark.integration, pytest.mark.feature("DB-SCHEMA")]


@pytest.mark.parametrize("reimports", [1, 2])
def test_complete_schema_creates_tables_and_preserves_data(cluster, reimports):
    schema = "schema_" + secrets.token_hex(8)
    initializer = (
        (ROOT / "database/mysql/schema.sql")
        .read_text(encoding="utf-8-sig")
        .replace("`cppchat`", f"`{schema}`")
    )
    connection = pymysql.connect(
        host=cluster.mysql_host,
        port=cluster.mysql_port,
        user="root",
        password=cluster.root_password,
        charset="utf8mb4",
        autocommit=True,
        client_flag=CLIENT.MULTI_STATEMENTS,
    )
    with connection:
        with connection.cursor() as cursor:
            try:
                cursor.execute(initializer)
                while cursor.nextset():
                    pass
                cursor.execute("SHOW TABLES")
                tables = {row[0] for row in cursor.fetchall()}
                expected_counts = {
                    "users": 2,
                    "user_avatars": 1,
                    "friend_apply": 1,
                    "friend": 1,
                    "chat_thread": 2,
                    "private_chat": 1,
                    "group_chat_member": 1,
                    "chat_message": 1,
                }
                require(tables == set(expected_counts), "Incomplete project schema")
                users = []
                for number in range(2):
                    cursor.execute(
                        "INSERT INTO users(username,email,nick,password_hash,password_salt) "
                        "VALUES(%s,%s,%s,%s,%s)",
                        (
                            f"Schema{number}",
                            f"schema{number}@example.test",
                            "昵称😀",
                            "0" * 64,
                            "1" * 32,
                        ),
                    )
                    users.append(cursor.lastrowid)
                first, second = users
                cursor.execute(
                    "INSERT INTO user_avatars(uid,image) VALUES(%s,%s)", (first, b"avatar")
                )
                cursor.execute(
                    "INSERT INTO friend_apply(from_uid,to_uid,tags) VALUES(%s,%s,%s)",
                    (first, second, '["同事"]'),
                )
                cursor.execute(
                    "INSERT INTO friend(self_id,friend_id,tags) VALUES(%s,%s,%s)",
                    (first, second, '["同事"]'),
                )
                cursor.execute("INSERT INTO chat_thread(type) VALUES('private')")
                private_thread = cursor.lastrowid
                cursor.execute(
                    "INSERT INTO private_chat(thread_id,user1_id,user2_id) VALUES(%s,%s,%s)",
                    (private_thread, first, second),
                )
                cursor.execute("INSERT INTO chat_thread(type) VALUES('group')")
                cursor.execute(
                    "INSERT INTO group_chat_member(thread_id,user_id) VALUES(%s,%s)",
                    (cursor.lastrowid, first),
                )
                cursor.execute(
                    "INSERT INTO chat_message(thread_id,sender_id,recv_id,unique_id,content,"
                    "msg_type,media_size) VALUES(%s,%s,%s,%s,%s,1,1024)",
                    (private_thread, first, second, "schema-probe", "image-probe"),
                )
                for _ in range(reimports):
                    cursor.execute(initializer)
                    while cursor.nextset():
                        pass
                for table, count in expected_counts.items():
                    cursor.execute(f"SELECT COUNT(*) FROM `{table}`")
                    require(cursor.fetchone()[0] == count, f"Reimport changed {table} data")
                cursor.execute("SELECT nick FROM users WHERE id=%s", (first,))
                require(cursor.fetchone()[0] == "昵称😀", "UTF-8 profile not preserved")
                cursor.execute("SELECT tags FROM friend WHERE self_id=%s", (first,))
                require(cursor.fetchone()[0] == '["同事"]', "Contact tags not preserved")
                cursor.execute("SELECT msg_type,media_size FROM chat_message")
                require(cursor.fetchone() == (1, 1024), "Image metadata not preserved")
                cursor.execute(
                    "SELECT COLUMN_NAME,SUB_PART,NON_UNIQUE FROM information_schema.STATISTICS "
                    "WHERE TABLE_SCHEMA=%s AND TABLE_NAME='chat_message' "
                    "AND INDEX_NAME='idx_chat_message_retry' ORDER BY SEQ_IN_INDEX",
                    (schema,),
                )
                require(
                    list(cursor.fetchall())
                    == [("thread_id", None, 1), ("sender_id", None, 1), ("unique_id", None, 1)],
                    "Retry lookup index is missing or incompatible",
                )
                cursor.execute(
                    "SELECT CHARACTER_MAXIMUM_LENGTH FROM information_schema.COLUMNS "
                    "WHERE TABLE_SCHEMA=%s AND TABLE_NAME='chat_message' "
                    "AND COLUMN_NAME='unique_id'",
                    (schema,),
                )
                require(cursor.fetchone()[0] == 64, "Message UUID storage limit changed")
                with pytest.raises(pymysql.IntegrityError) as duplicate:
                    cursor.execute(
                        "INSERT INTO users(username,email,password_hash,password_salt) "
                        "VALUES(%s,%s,%s,%s)",
                        ("Schema0", "duplicate@example.test", "0" * 64, "1" * 32),
                    )
                require(duplicate.value.args[0] == 1062, "Username uniqueness not enforced")
                with pytest.raises(pymysql.IntegrityError) as invalid_friend:
                    cursor.execute(
                        "INSERT INTO friend(self_id,friend_id) VALUES(%s,%s)", (first, second + 100)
                    )
                require(invalid_friend.value.args[0] == 1452, "Friend foreign key not enforced")
            finally:
                cursor.execute(f"DROP DATABASE IF EXISTS `{schema}`")
