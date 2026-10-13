"""Stdlib-only report generation also works when testing dependencies are absent."""

from collections import Counter
import html
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def junit_cases(path):
    cases = []
    if not Path(path).is_file():
        return cases
    for item in ET.parse(path).iter("testcase"):
        name = item.get("classname", "") + "." + item.get("name", "")
        status, reason = "PASS", ""
        for tag in ("error", "failure", "skipped"):
            child = item.find(tag)
            if child is not None:
                reason = child.get("message", "") + (child.text or "")
                status = (
                    "FAIL"
                    if tag != "skipped"
                    else ("未实现" if "NOT_IMPLEMENTED" in reason else "BLOCKED")
                )
                break
        cases.append(
            {"name": name, "status": status, "reason": reason, "seconds": item.get("time", "0")}
        )
    return cases


def write_report(directory, stages, metadata):
    cases = []
    for path in directory.glob("**/*junit.xml"):
        for case in junit_cases(path):
            case["source"] = str(path.relative_to(directory))
            cases.append(case)
    totals = Counter(item["status"] for item in cases)
    state = (
        "FAIL"
        if any(s["status"] == "FAIL" for s in stages) or totals["FAIL"]
        else (
            "BLOCKED"
            if any(s["status"] == "BLOCKED" for s in stages) or totals["BLOCKED"]
            else "PASS"
        )
    )
    data = {"status": state, "stages": stages, "counts": dict(totals), "cases": cases, **metadata}
    (directory / "summary.json").write_text(
        json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8"
    )

    def escape(value):
        return html.escape(str(value))

    rows = "".join(
        f"<tr><td>{escape(c['name'])}</td><td>{c['status']}</td><td><pre>{escape(c['reason'])}</pre></td></tr>"
        for c in cases
    )
    stage_rows = "".join(
        f"<li>{escape(s['name'])}: <b>{s['status']}</b> {escape(s.get('reason', ''))}</li>"
        for s in stages
    )
    (directory / "index.html").write_text(
        f"""<!doctype html><html lang="zh"><meta charset="utf-8">
<title>CppChat 回归报告</title><style>body{{font:16px system-ui;margin:32px;max-width:1400px}}td,th{{padding:8px;border:1px solid #ddd}}table{{border-collapse:collapse;width:100%}}pre{{white-space:pre-wrap;max-width:900px}}b{{color:#2563eb}}</style>
<h1>CppChat {escape(metadata["suite"])} — {state}</h1><p>本轮：{escape(metadata["run_id"])}；随机种子：{metadata["seed"]}</p>
<p>PASS / FAIL / BLOCKED / 未实现 分开统计。人工验收缺失不计为通过。历史文档不参与结果。</p>
<p>人工待验收：{escape(metadata.get("pending_acceptance", []))}；覆盖率：{escape(metadata.get("coverage", "未运行"))}；功能实现状态：{escape(metadata.get("implementation_status", {}))}</p>
<ul>{stage_rows}</ul><p>{escape(dict(totals))}</p><table><tr><th>用例</th><th>结果</th><th>证据</th></tr>{rows}</table></html>""",
        encoding="utf-8",
    )
    # A combined JUnit makes native/missing-environment stages visible to CI.
    root = ET.Element("testsuites")
    suite = ET.SubElement(root, "testsuite", name="CppChat", tests=str(len(stages) + len(cases)))
    for entry in stages + cases:
        case = ET.SubElement(
            suite, "testcase", name=entry["name"], time=str(entry.get("seconds", 0))
        )
        if entry["status"] != "PASS":
            ET.SubElement(
                case,
                "failure" if entry["status"] == "FAIL" else "skipped",
                message=entry.get("reason", entry["status"]),
            )
    ET.ElementTree(root).write(directory / "combined.xml", encoding="utf-8", xml_declaration=True)
    return state
