"""Group permissions and delivery use real SQL, Redis and both chat nodes."""

import uuid

import pytest

from harness.checks import require
from test_journey import history, send

pytestmark = [pytest.mark.integration, pytest.mark.feature("GROUP-CHAT")]


def change(peer, op, thread=0, **fields):
    return peer.request(1060, dict(fields, op=op, thread_id=thread))


def create(peer, members, name="项目讨论组", key=None):
    return change(peer, "create", members=members, name=name, creation_key=key or str(uuid.uuid4()))


@pytest.mark.smoke
def test_group_lifecycle_cross_node_offline_and_permissions(cluster, pair):
    first, second, a, b, private = pair
    third, outsider = cluster.register(), cluster.register()
    c, stranger = cluster.login(third, "chat1"), cluster.login(outsider, "chat2")
    cluster.friends(first, third, a, c)
    key = str(uuid.uuid4())
    created = create(a, [second["uid"]], key=key)
    require(created.get("error") == 0, "Group creation failed")
    group = created["group"]
    thread = group["thread_id"]
    require(group["owner_uid"] == first["uid"], "Creator must be owner")
    require(len(group["members"]) == 2, "Initial members missing")
    require(
        create(a, [second["uid"]], key=key)["group"]["thread_id"] == thread,
        "Retry duplicated group",
    )
    require(
        create(a, [third["uid"]], key=key).get("error") != 0,
        "Creation key reused with different payload",
    )
    require(change(stranger, "info", thread).get("error") != 0, "Outsider read group metadata")
    require(change(b, "rename", thread, name="forbidden").get("error") != 0, "Member renamed group")
    require(
        change(b, "add", thread, member_uid=third["uid"]).get("error") != 0, "Member invited users"
    )
    require(change(a, "leave", thread).get("error") == 1019, "Owner left group without transfer")
    require(
        change(a, "add", thread, member_uid=outsider["uid"]).get("error") != 0, "Nonfriend invited"
    )
    require(
        change(a, "add", thread, member_uid=third["uid"]).get("error") == 0, "Friend invite failed"
    )
    require(change(a, "rename", thread, name="新的群名").get("error") == 0, "Rename failed")
    group_reply = b.request(1025, {"uid": second["uid"], "thread_id": 0})
    require(
        any(
            t["type"] == "group" and t["group"]["name"] == "新的群名"
            for t in group_reply["threads"]
        ),
        "Group missing from conversations",
    )
    group_target = {"uid": 0}
    message_key = str(uuid.uuid4())
    sent = send(a, first, group_target, thread, "群聊中文 English 😀", message_key)
    require(sent.get("error") == 0, "Group send failed")
    repeated = send(a, first, group_target, thread, "群聊中文 English 😀", message_key)
    require(
        repeated["chat_datas"][0]["message_id"] == sent["chat_datas"][0]["message_id"],
        "Group retry duplicated message",
    )
    for peer in (b, c):
        peer.request(1023, {})
        require(
            any(
                mid == 1019 and body.get("thread_id") == thread for mid, body in peer.notifications
            ),
            "Group live fanout missing",
        )
        require(len(history(peer, thread)) == 1, "Group history or dedupe failed")
    # Member-to-member friendship is not required: b and c are both current members.
    require(
        send(b, second, group_target, thread, "普通成员发言").get("error") == 0,
        "Member send rejected",
    )
    require(
        send(stranger, outsider, group_target, thread, "forged").get("error") != 0,
        "Outsider sent group message",
    )
    require(
        send(a, first, group_target, private, "forged private").get("error") != 0,
        "Group path accepted private thread",
    )
    require(
        send(a, first, second, thread, "forged receiver").get("error") != 0,
        "Private path accepted group thread",
    )
    require(
        send(a, first, group_target, thread, "a" * 32, msg_type=1).get("error") != 0,
        "Unscoped group image accepted",
    )
    c.close()
    require(
        send(a, first, group_target, thread, "离线补拉").get("error") == 0,
        "Offline group message failed",
    )
    c = cluster.login(third, "chat1")
    require(
        any(m["content"] == "离线补拉" for m in history(c, thread)), "Offline member lost history"
    )
    require(change(a, "remove", thread, member_uid=third["uid"]).get("error") == 0, "Remove failed")
    require(
        c.request(1029, {"thread_id": thread, "message_id": 0}).get("error") != 0,
        "Removed member read history",
    )
    require(
        send(c, third, group_target, thread, "after removal").get("error") != 0,
        "Removed member sent message",
    )
    require(
        change(a, "transfer", thread, member_uid=second["uid"]).get("error") == 0, "Transfer failed"
    )
    require(change(a, "disband", thread).get("error") != 0, "Former owner disbanded")
    require(change(a, "leave", thread).get("error") == 0, "Member leave failed")
    require(change(b, "disband", thread).get("error") == 0, "New owner disband failed")
    require(change(b, "info", thread).get("error") != 0, "Disbanded group visible")
    require(
        send(b, second, group_target, thread, "after disband").get("error") != 0,
        "Disbanded group accepted send",
    )
    with cluster.sql() as db, db.cursor() as cursor:
        cursor.execute(
            "SELECT COUNT(*) FROM chat_message WHERE thread_id=%s AND unique_id=%s",
            (thread, message_key),
        )
        require(cursor.fetchone()[0] == 1, "Retry must persist one group message")
        cursor.execute("SELECT COUNT(*) FROM group_chat_member WHERE thread_id=%s", (thread,))
        require(cursor.fetchone()[0] == 0, "Disband retained members")
    c.close()
    stranger.close()


@pytest.mark.parametrize("bad", [None, "", " " * 4, "中" * 22, "bad\x00name", 123, {}])
def test_invalid_group_names(cluster, pair, bad):
    first, second, a, b, _ = pair
    require(
        create(a, [second["uid"]], name=bad).get("error") == 1001, "Invalid group name accepted"
    )


def test_group_membership_limit_and_validation(cluster, pair):
    first, second, a, b, _ = pair
    for invalid in ([], [first["uid"]], [None], ["bad"], [0], [-1], [second["uid"]] * 32):
        require(create(a, invalid).get("error") != 0, "Invalid member selection accepted")
    require(change(a, "unknown").get("error") == 1001, "Unknown operation accepted")


def test_full_group_frame_budget_and_invitation_limit(cluster, pair):
    first, second, a, b, _ = pair
    ids = [second["uid"]]
    with cluster.sql() as db, db.cursor() as cursor:
        for number in range(31):
            identity = uuid.uuid4().hex
            cursor.execute(
                "INSERT INTO users(username,email,password_hash,password_salt) VALUES(%s,%s,%s,%s)",
                ("中" * 20 + str(number), identity + "@example.test", "0" * 64, "1" * 32),
            )
            uid = cursor.lastrowid
            cursor.execute(
                "INSERT INTO friend(self_id,friend_id) VALUES(%s,%s)", (first["uid"], uid)
            )
            ids.append(uid)
    # 32 members including the creator, with long multibyte names. Info must fit
    # the real 16 KiB frame, not disappear in CSession::Send.
    group = create(a, ids[:31], name="中" * 21)
    require(group.get("error") == 0, "Full group response exceeded frame budget")
    thread = group["group"]["thread_id"]
    require(
        len(change(a, "info", thread)["group"]["members"]) == 32, "Full group metadata truncated"
    )
    require(
        change(a, "add", thread, member_uid=ids[-1]).get("error") == 1020, "Member cap not enforced"
    )
    require(
        send(a, first, {"uid": 0}, thread, "full group message").get("error") == 0,
        "Full group send failed",
    )
    require(change(a, "disband", thread).get("error") == 0, "Full group cleanup failed")
