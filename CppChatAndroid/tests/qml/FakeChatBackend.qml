import QtQml

QtObject {
    property bool isLoggedIn: true
    property bool sending: false
    property bool storeReady: false
    property string currentNick: "小林"
    property string currentUser: "lin"
    property var mediaCapabilities: ({
                                         voice: true,
                                         files: true,
                                         call: true,
                                         video: true
                                     })
    property var sent: []
    property var drafts: []
    property var lastRetry: []
    property var rows: []
    property var files: ({})
    property var threads: []
    property bool group: false
    property QtObject media: QtObject {
        property int recordingThread: -1
        property bool recording: false
        property bool permissionPending: false
        property int draftMs: 0
        property int recordingMs: 0
        property string playing: ""
        property var call: ({
                                state: 0,
                                video: false,
                                name: "小陈",
                                seconds: 61,
                                recovering: false
                            })
        function leaveChat(thread) {
        }
        function endCall() {
            call = {
                state: 0
            };
        }
        function setVideoSinks(local, remote) {
        }
    }
    function currentUid() {
        return 100;
    }
    function avatarSource(uid, fallback) {
        return "";
    }
    function imageSource(fileId) {
        return "";
    }
    function threadInfo(id) {
        return {
            isGroup: group,
            pinned: false
        };
    }
    function groupInfo(id) {
        return {
            members: [
                {
                    uid: 102,
                    nick: "阿明"
                }
            ]
        };
    }
    function messagesForThread(id) {
        return rows;
    }
    function loadMessages(id, before, count) {
    }
    function loadDraft(id) {
    }
    function saveDraft(id, text) {
        drafts.push(text);
    }
    function sendTextMessage(id, text) {
        sent.push(text);
    }
    function retryMessage(id, uniqueId) {
        lastRetry = [id, uniqueId];
    }
    function chatThreads() {
        return threads;
    }
    function friendList() {
        return [
                    {
                        uid: 101,
                        name: "chen",
                        displayName: "小陈",
                        icon: ""
                    }
                ];
    }
    function fileInfo(id) {
        if (files[id]) return files[id];
        return {
            ready: false,
            name: "",
            size: 0
        };
    }
    signal messagesLoaded(int thread)
    signal historyCleared(int thread)
    signal draftLoaded(int thread, string text)
    signal textMessageSaved(int thread, string text)
    signal localHistoryLoaded(int thread, bool more)
    signal contactsChanged
    signal chatThreadsLoaded
    signal imageReady(string id)
    signal avatarReady(int uid)
    signal fileReady(string id)
    signal operationFailed(string message)
    signal loginStatusChanged
    signal transferProgress(string id, double done, double total, bool upload)
}
