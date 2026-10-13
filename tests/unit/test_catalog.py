import json
from pathlib import Path
from harness.checks import require

ROOT = Path(__file__).resolve().parents[1]


def test_every_feature_has_reviewable_case_and_cleanup():
    rows = json.loads((ROOT / "cases.json").read_text(encoding="utf-8"))
    require(len({row["id"] for row in rows}) == len(rows), "Duplicate feature ID")
    for row in rows:
        for field in (
            "domain",
            "preconditions",
            "inputs",
            "expected",
            "data_changes",
            "cleanup",
            "automation",
        ):
            require(row.get(field), f"{row['id']} lacks {field}")
        require(
            (ROOT / row["automation"]).is_file(), f"{row['id']} references missing implementation"
        )


def test_migrations_are_outside_business_source():
    repository = ROOT.parent
    for directory in (
        "GateServer",
        "ChatServer",
        "ChatServer2",
        "ChatServerShared",
        "StatusServer",
        "VerifyServer",
        "CppChat",
    ):
        require(
            not list((repository / directory).rglob("*.sql")),
            "SQL leaked into business source: " + directory,
        )
