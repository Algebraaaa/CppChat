"""Preflight/apply/roll back the additive MySQL message idempotency constraint.

Uses the project's locked PyMySQL dependency. By default only checks metadata and
duplicate counts. Never deletes messages or resolves conflicting payloads itself.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re

import pymysql
from pymysql.constants import SERVER_STATUS

INDEX = "uk_chat_message_idempotency"
COLUMN = "dedup_unique_id"
MIGRATION = "20261015_message_idempotency"


class MigrationBlocked(RuntimeError):
    pass


def normalized_expression(value):
    # Preserve function/condition parentheses: LENGTH(unique_id=0) and
    # LENGTH(unique_id)=0 have very different meanings.
    return re.sub(r"[\s`]+", "", value.lower()).replace("octet_length", "length")


def inspect(connection):
    with connection.cursor() as cursor:
        cursor.execute("SELECT DATABASE()")
        schema = cursor.fetchone()[0]
        if not schema:
            raise MigrationBlocked("Select an explicit database first")
        cursor.execute(
            "SELECT COLUMN_NAME,COLUMN_TYPE,IS_NULLABLE,COLLATION_NAME,EXTRA,GENERATION_EXPRESSION "
            "FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=%s AND TABLE_NAME='chat_message' "
            "AND COLUMN_NAME IN ('unique_id','dedup_unique_id')",
            (schema,),
        )
        columns = {row[0]: row[1:] for row in cursor.fetchall()}
        original = columns.get("unique_id")
        if not original or original[0].lower() != "varchar(64)" or original[1] != "NO":
            raise MigrationBlocked("Unexpected unique_id definition")
        cursor.execute(
            "SELECT ENGINE,TABLE_COLLATION FROM information_schema.TABLES "
            "WHERE TABLE_SCHEMA=%s AND TABLE_NAME='chat_message'",
            (schema,),
        )
        table = cursor.fetchone()
        if not table or table[0] != "InnoDB" or table[1] != original[2]:
            raise MigrationBlocked(
                "Table engine/collation requires a separate migration"
            )
        generated = columns.get(COLUMN)
        if generated and (
            generated[0].lower() != "varchar(64)"
            or generated[1] != "YES"
            or generated[2] != original[2]
            or "VIRTUAL GENERATED" not in generated[3]
            or normalized_expression(generated[4])
            not in {
                "(casewhen(length(unique_id)=0)thennullelseunique_idend)",
                "casewhen(length(unique_id)=0)thennullelseunique_idend",
                "(casewhenlength(unique_id)=0thennullelseunique_idend)",
                "casewhenlength(unique_id)=0thennullelseunique_idend",
            }
        ):
            raise MigrationBlocked(
                "Reserved generated column has a different definition"
            )
        cursor.execute(
            "SELECT COLUMN_NAME,SUB_PART,NON_UNIQUE,IS_VISIBLE,INDEX_TYPE "
            "FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=%s "
            "AND TABLE_NAME='chat_message' AND INDEX_NAME=%s ORDER BY SEQ_IN_INDEX",
            (schema, INDEX),
        )
        index = list(cursor.fetchall())
        expected = [
            (name, None, 0, "YES", "BTREE")
            for name in ("thread_id", "sender_id", COLUMN)
        ]
        if index and (index != expected or not generated):
            raise MigrationBlocked("Reserved unique index has a different definition")
        cursor.execute(
            "SELECT COUNT(*) FROM (SELECT 1 FROM chat_message WHERE OCTET_LENGTH(unique_id)>0 "
            "GROUP BY thread_id,sender_id,unique_id HAVING COUNT(*)>1) conflicts"
        )
        duplicates = cursor.fetchone()[0]
        cursor.execute(
            "SELECT COUNT(*),COALESCE(SUM(OCTET_LENGTH(unique_id)=0),0) FROM chat_message"
        )
        rows, system_rows = cursor.fetchone()
        return {
            "database": schema,
            "column_present": bool(generated),
            "index_present": bool(index),
            "duplicate_groups": int(duplicates),
            "message_rows": int(rows),
            "system_rows": int(system_rows),
        }


def migrate(connection, *, direction="up", apply=False):
    if direction not in ("up", "down"):
        raise ValueError("Unknown migration direction")
    if (
        not connection.get_autocommit()
        or connection.server_status & SERVER_STATUS.SERVER_STATUS_IN_TRANS
    ):
        raise MigrationBlocked(
            "Use a dedicated autocommit connection; DDL implicitly commits"
        )
    with connection.cursor() as cursor:
        cursor.execute("SELECT DATABASE(),@@SESSION.lock_wait_timeout")
        schema, previous_timeout = cursor.fetchone()
        if not schema:
            raise MigrationBlocked("Select an explicit database first")
        lock = (
            "cppchat:message-idempotency:"
            + hashlib.sha256(schema.encode()).hexdigest()[:24]
        )
        cursor.execute("SELECT GET_LOCK(%s,0)", (lock,))
        if cursor.fetchone()[0] != 1:
            raise MigrationBlocked("Another migration owns this database")
    try:
        before = inspect(connection)
        if direction == "up" and before["duplicate_groups"]:
            raise MigrationBlocked(
                f"{before['duplicate_groups']} duplicate message groups; no rows changed"
            )
        report = {
            "migration": MIGRATION,
            "direction": direction,
            "before": before,
            "steps": [],
        }
        if not apply:
            return dict(report, status="preflight")
        path = (
            Path(__file__).parent
            / "migrations"
            / (MIGRATION + "." + direction + ".sql")
        )
        statements = [
            part.strip()
            for part in path.read_text(encoding="utf-8").split(";")
            if part.strip()
        ]
        if len(statements) != 2:
            raise MigrationBlocked("Unexpected migration SQL layout")
        with connection.cursor() as cursor:
            cursor.execute("SET SESSION lock_wait_timeout=10")
            if direction == "up":
                needed = [not before["column_present"], not before["index_present"]]
                labels = ["add_column", "add_index"]
            else:
                needed = [before["index_present"], before["column_present"]]
                labels = ["drop_index", "drop_column"]
            for statement, change, label in zip(statements, needed, labels):
                if change:
                    cursor.execute(statement)
                    report["steps"].append(label)
        after = inspect(connection)
        enabled = direction == "up"
        if after["column_present"] != enabled or after["index_present"] != enabled:
            raise MigrationBlocked(
                "Post-migration metadata does not match the requested state"
            )
        return dict(
            report,
            status="applied" if report["steps"] else "already-current",
            after=after,
        )
    finally:
        if connection.open:
            with connection.cursor() as cursor:
                cursor.execute("SET SESSION lock_wait_timeout=%s", (previous_timeout,))
                cursor.execute("SELECT RELEASE_LOCK(%s)", (lock,))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", required=True)
    parser.add_argument("--socket", default="/var/run/mysqld/mysqld.sock")
    parser.add_argument("--defaults-file", type=Path)
    parser.add_argument("--direction", choices=("up", "down"), default="up")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        options = {
            "user": "root",
            "unix_socket": args.socket,
            "database": args.database,
            "autocommit": True,
            "read_timeout": 300,
            "write_timeout": 10,
        }
        if args.defaults_file:
            options["read_default_file"] = str(args.defaults_file)
        with pymysql.connect(**options) as connection:
            report = migrate(connection, direction=args.direction, apply=args.apply)
    except MigrationBlocked as error:
        report = {"status": "blocked", "reason": str(error)}
    except pymysql.MySQLError as error:
        # Connector errors can include message identifiers/content. Print only the
        # code; after a lost DDL connection, inspect state rather than replaying.
        report = {
            "status": "error",
            "mysql_code": error.args[0],
            "reason": "Inspect migration state before retrying; existing rows were not deleted",
        }
    encoded = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded)
    return int(report["status"] in ("blocked", "error"))


if __name__ == "__main__":
    raise SystemExit(main())
