import QtQuick
import QtQuick.Controls.Material
import QtTest
import "../../qml/pages"
import "../../qml/components"
import "../../qml/components/ChatStyle.js" as Style

Item {
    width: 800
    height: 780
    FontLoader {
        id: chineseFont
        source: "../../../CppChat/Resources/fonts/SourceHanSansCN-Regular.otf"
    }
    ApplicationWindow {
        id: host
        width: 390
        height: 780
        visible: true
        Material.theme: Material.Light
        font.family: chineseFont.name
    }
    FakeChatBackend {
        id: fakeBackend
    }
    QtObject {
        id: navigation
        function back() {
        }
        function push(name, properties) {
        }
    }
    Component {
        id: bubbleComponent
        MessageBubble {}
    }
    Component {
        id: detailComponent
        ChatDetailPage {}
    }
    Component {
        id: listComponent
        ChatListPage {}
    }
    Component {
        id: callComponent
        CallPanel {}
    }
    TestCase {
        name: "AndroidChatUi"
        when: windowShown
        property var backend: fakeBackend
        function init() {
            failOnWarning(/.*/);
            host.width = 390;
            host.height = 780;
            backend.isLoggedIn = true;
            backend.sending = false;
            backend.sent = [];
            backend.drafts = [];
            backend.rows = [];
            backend.threads = [];
            backend.group = false;
            backend.media.call = {
                state: 0,
                video: false,
                name: "小陈",
                seconds: 61,
                camera: false,
                front: false,
                muted: false
            };
        }
        function fixture(content, self, id, time, type) {
            return {
                msgId: id === undefined ? 1 : id,
                uniqueId: String(id),
                senderUid: self ? 100 : 101,
                isSelf: !!self,
                type: type || 0,
                status: 2,
                time: time || "2026-10-24 09:00:00",
                content: content
            };
        }
        function page(component, width, height) {
            host.width = width || 390;
            host.height = height || 780;
            let item = createTemporaryObject(component, host.contentItem, {
                                                 backend: backend,
                                                 navigation: navigation,
                                                 width: host.width,
                                                 height: host.height
                                             });
            verify(item !== null);
            return item;
        }
        function test_naturalBubblesAndPlainText() {
            let short = createTemporaryObject(bubbleComponent, host.contentItem, {
                                                  backend: backend,
                                                  message: fixture("好的", false),
                                                  maximumWidth: 208
                                              });
            let long = createTemporaryObject(bubbleComponent, host.contentItem, {
                                                 backend: backend,
                                                 message: fixture(
                                                              "窄屏聊天气泡应自动换行，中文、English 和很长的链接都不能超出屏幕。",
                                                              true),
                                                 maximumWidth: 208,
                                                 y: 100
                                             });
            verify(short.width < long.width);
            verify(long.width <= 208);
            tryVerify(function () {
                return long.height > short.height;
            });
            let text = findChild(long, "messageText");
            compare(text.readOnly, true);
            compare(text.textFormat, TextEdit.PlainText);
            long.message = fixture("<b>按原文显示</b>\n第二行", true);
            compare(text.text, "<b>按原文显示</b>\n第二行");
        }
        function test_timeGroupingAndInvalidDates() {
            compare(Style.showTime(fixture("一", false, 1), null), true);
            compare(Style.showTime(fixture("二", false, 2, "2026-10-24 09:04:59"), fixture("一", false,
                                                                                          1)), false);
            compare(Style.showTime(fixture("二", false, 2, "2026-10-24 09:05:00"), fixture("一", false,
                                                                                          1)), true);
            compare(Style.showTime(fixture("二", false, 2, "2026-10-25 00:00:01"), fixture("一", false,
                                                                                          1, "2026-10-24 23:59:59")),
                    true);
            compare(Style.timeLabel("garbage"), "");
            compare(Style.timeLabel("2026-09-30 23:59:00", new Date(2026, 9, 1, 8)), "昨天 23:59");
        }
        function test_composer_data() {
            return [
                        {
                            tag: "narrow",
                            w: 320,
                            h: 480
                        },
                        {
                            tag: "phone",
                            w: 390,
                            h: 780
                        },
                        {
                            tag: "landscape",
                            w: 800,
                            h: 480
                        }
                    ];
        }
        function test_textMenuAndPendingAttachment() {
            let bubble = createTemporaryObject(bubbleComponent, host.contentItem, {
                                                   backend: backend,
                                                   message: fixture("可以长按复制的消息", false),
                                                   maximumWidth: 240
                                               });
            let text = findChild(bubble, "messageText");
            mousePress(text, 16, 12);
            wait(900);
            mouseRelease(text, 16, 12);
            let menu = findChild(bubble, "messageMenu");
            tryCompare(menu, "opened", true);
            menu.close();
            tryCompare(menu, "visible", false);
            bubble.message = fixture("attachment", true, 0, undefined, 2);
            let action = findChild(bubble, "attachmentAction");
            compare(action.enabled, false);
        }
        function test_composer(data) {
            let detail = page(detailComponent, data.w, data.h);
            let input = findChild(detail, "messageInput");
            let send = findChild(detail, "sendButton");
            let composer = findChild(detail, "composer");
            compare(send.visible, false);
            input.text = "要保存成功后再清空的草稿";
            tryCompare(send, "visible", true);
            verify(input.width >= 60);
            verify(composer.height >= 56);
            detail.send();
            compare(backend.sent.length, 1);
            compare(input.text, backend.sent[0]);
            backend.textMessageSaved(0, "另一个在途消息");
            compare(input.text, backend.sent[0]);
            backend.textMessageSaved(0, backend.sent[0]);
            compare(input.text, "");
            input.text = "离线草稿";
            backend.isLoggedIn = false;
            compare(send.enabled, false);
            detail.send();
            compare(backend.sent.length, 1);
            input.text = "多行\n输入\n应滚动\n不超高\n保留草稿";
            verify(composer.height <= 150);
        }
        function test_groupNamesAndAlignment() {
            backend.group = true;
            let detail = page(detailComponent);
            let a = fixture("周末一起去走走？", false, 1);
            let b = fixture("好呀，下午见。", true, 2, "2026-10-24 09:00:30");
            let c = fixture("我也来！", false, 3, "2026-10-24 09:06:00");
            c.senderUid = 102;
            detail.messages = [a, b, c];
            let list = findChild(detail, "messageList");
            list.forceLayout();
            tryVerify(function () {
                return list.itemAtIndex(0) !== null;
            });
            compare(detail.senderName(a), "小陈");
            compare(detail.senderName(c), "阿明");
            compare(list.itemAtIndex(0).hasTime, true);
            compare(list.itemAtIndex(1).hasTime, false);
            compare(list.itemAtIndex(2).hasTime, true);
            let incoming = findChild(detail, "bubble-0");
            let outgoing = findChild(detail, "bubble-1");
            verify(incoming.x >= 60);
            verify(outgoing.x + outgoing.width <= detail.width - 60);
            verify(outgoing.x > incoming.x);
        }
        function test_attachmentPanelAndSearch() {
            let detail = page(detailComponent);
            let toggle = findChild(detail, "attachmentToggle");
            mouseClick(toggle);
            compare(detail.attachmentsVisible, true);
            verify(findChild(detail, "attachmentPanel").height < 140);
            detail.destroy();
            backend.threads = [
                        {
                            threadId: 1,
                            otherId: 101,
                            name: "小陈",
                            icon: "",
                            lastMsg: "周末见",
                            lastTime: "2026-10-24 09:00:00",
                            pinned: true
                        },
                        {
                            threadId: 2,
                            otherId: 102,
                            name: "工作群",
                            icon: "",
                            lastMsg: "会议纪要",
                            lastTime: "2026-10-23 09:00:00",
                            pinned: false
                        }
                    ];
            let home = page(listComponent);
            compare(home.filteredThreads.length, 2);
            findChild(home, "searchField").text = "周末";
            compare(home.filteredThreads.length, 1);
            findChild(home, "mainTabs").currentIndex = 1;
            compare(findChild(home, "searchField").text, "");
            compare(home.filteredContacts.length, 1);
        }
        function test_readingHistoryAndReturnToLatest() {
            let messages = [];
            for (let i = 0; i < 30; ++i)
                messages.push(fixture("历史消息 " + i, i % 2 === 0, i + 1));
            backend.rows = messages;
            let detail = page(detailComponent, 320, 480);
            let list = findChild(detail, "messageList");
            tryCompare(list, "count", 30);
            tryCompare(list, "atYEnd", true);
            list.stickToEnd = false;
            list.forceLayout();
            list.positionViewAtBeginning();
            wait(30);
            verify(!list.atYEnd);
            const oldY = list.contentY;
            backend.rows = [fixture("新消息", false, 31)];
            detail.refresh();
            wait(30);
            fuzzyCompare(list.contentY, oldY, 1);
            let latest = findChild(detail, "latestMessageButton");
            compare(latest.visible, true);
            mouseClick(latest);
            wait(80);
            tryCompare(list, "atYEnd", true);
        }
        function test_recoveryStatusCompatibility() {
            let call = createTemporaryObject(callComponent, host.contentItem, {
                                                 backend: backend
                                             });
            backend.media.call = {
                state: 4,
                name: "小陈",
                video: false,
                seconds: 61,
                recovering: true,
                camera: false,
                front: false,
                muted: false
            };
            compare(findChild(call.contentItem, "callStatus").text, "网络波动，正在恢复…");
            backend.media.call = {
                state: 4,
                name: "小陈",
                video: false,
                seconds: 61,
                camera: false,
                front: false,
                muted: false
            };
            compare(findChild(call.contentItem, "callStatus").text, "通话中 1:01");
            backend.media.call = {
                state: 0,
                video: false,
                name: "小陈",
                seconds: 61,
                camera: false,
                front: false,
                muted: false
            };
            tryCompare(call, "visible", false);
        }
        function test_visualSnapshots_data() {
            return [
                        {
                            tag: "narrow",
                            w: 320,
                            h: 480
                        },
                        {
                            tag: "phone",
                            w: 390,
                            h: 780
                        },
                        {
                            tag: "landscape",
                            w: 800,
                            h: 480
                        }
                    ];
        }
        function saveSnapshot(item, name) {
            const url = Qt.resolvedUrl("../../../build/android-ui/screenshots/" + name
                                       + ".png").toString();
            const path = decodeURIComponent(url.replace(Qt.platform.os === "windows" ?
                                                            /^file:\/\/\// : /^file:\/\//, ""));
            wait(30);
            const rendered = grabImage(item);
            verify(rendered.width > 0 && rendered.height > 0);
            rendered.save(path);
        }
        function test_visualSnapshots(data) {
            let detail = page(detailComponent, data.w, data.h);
            detail.title = "小陈";
            detail.messages = [fixture("周末一起去走走？", false, 1), fixture("好呀！公园门口见。", true, 2,
                                                                      "2026-10-24 09:00:30"),
                               fixture("这次的聊天气泡会根据内容自适应宽度，长消息也能自然换行。", false, 3,
                                       "2026-10-24 09:06:00"), fixture("收到，下午见！", true, 4,
                                                                       "2026-10-24 09:06:40")];
            wait(80);
            saveSnapshot(detail, "chat-" + data.tag);
            detail.destroy();
            backend.threads = [
                        {
                            threadId: 1,
                            otherId: 101,
                            name: "小陈",
                            icon: "",
                            lastMsg: "收到，下午见！",
                            lastTime: "2026-10-24 09:06:40",
                            pinned: true
                        },
                        {
                            threadId: 2,
                            otherId: 102,
                            name: "产品设计讨论群",
                            icon: "",
                            lastMsg: "阿明：新方案看起来不错",
                            lastTime: "2026-10-24 08:30:00",
                            pinned: false
                        },
                        {
                            threadId: 3,
                            otherId: 103,
                            name: "文件传输",
                            icon: "",
                            lastMsg: "[文件] 设计说明.pdf",
                            lastTime: "2026-10-23 18:00:00",
                            pinned: false
                        }
                    ];
            let home = page(listComponent, data.w, data.h);
            wait(80);
            saveSnapshot(home, "list-" + data.tag);
        }
    }
}
