// TioWindow.qml - the standalone window (path B).
//
// The same groups the search box shows, plus what a window can hold and the
// list cannot: a port dropdown named the way Device Manager names its ports,
// device details, an optional remote socket share, the session block, and Save.
// The pinned tio 3.9 has
// no DTR/RTS switch, so there is no control-lines card promising what cannot be
// kept.
//
// Nothing here claims the port is open. A started process is "已启动", the port
// state is whatever the session records (unknown unless something proves
// otherwise), and the footer says so in words.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Window {
    id: window

    property bool zh: typeof tio !== "undefined" ? tio.zh : true

    title: zh ? "tio 串口终端" : "tio serial terminal"
    minimumWidth: DesignTokens.minWidth
    minimumHeight: DesignTokens.minHeight
    width: 780
    height: 720
    color: DesignTokens.windowColor
    visible: true

    function tr(zhText, enText) { return window.zh ? zhText : enText }
    function notice(text) { footer.show(text) }

    // The model's own notices (saved, started, refused) go to the same footer.
    Connections {
        target: typeof tio !== "undefined" ? tio : null
        function onNotice(text) { footer.show(text) }
    }

    // "—" is what the model says when the tab names no port.
    readonly property bool hasPort: typeof tio !== "undefined" && tio.portText !== "—"

    // ---- the test hook -------------------------------------------------------
    // --auto <action> replays one button's work from a timer: no window, no
    // mouse. It is what the buttons do, so a click that hangs hangs here too;
    // a run under --no-show then leaves the desktop alone.
    function testAction(name) {
        if (typeof tio === "undefined" || !name)
            return
        if (name === "plus") {
            tio.newSession()
        } else if (name === "clone") {
            tio.cloneFocusedSession()
        } else if (name === "close") {
            // One tab at a time: tio.sessions is a live view, so the length is
            // read once and the index is taken before the close.
            const n = tio.sessions.length
            for (let i = n - 1; i >= 0; --i)
                if (tio.sessions[i].focused)
                    tio.closeSession(i)
        } else if (name === "share") {
            shareConfirm.open()
        } else if (name === "launch") {
            // Starts the session the way the button does, sharing included, so a
            // screenshot run can show a live shared socket instead of an empty
            // card. Test-only: the whole function is behind testAutoAction.
            tio.launch(true)
        } else if (name === "hex") {
            tio.shareFormat = "hex"
        } else if (name === "ascii") {
            tio.shareFormat = "ascii"
        } else if (name === "row8" || name === "row16" || name === "row32") {
            tio.shareHexRow = parseInt(name.substring(3), 10)
        } else if (name === "decode") {
            tio.shareHexAscii = !tio.shareHexAscii
        } else if (name === "logs") {
            logHistory.open()
        } else if (name === "copy") {
            tio.copyShareContent()
        } else if (name.startsWith("click:")) {
            // Press a real button by its objectName, so a run exercises the click
            // wiring instead of calling the model behind the button's back.
            const target = window.findChildByName(name.substring(6))
            if (target)
                target.clicked()
            else
                tio.trace("auto click: no button named " + name.substring(6))
        }
        tio.trace("auto " + name + " returned, tabs " + tio.sessions.length)
    }

    // A button by objectName, for the click steps above: the run then goes
    // through the button's own handler, not straight to the model.
    function findChildByName(name) {
        const stack = [window.contentItem]
        while (stack.length > 0) {
            const item = stack.pop()
            if (!item)
                continue
            if (item.objectName === name && typeof item.clicked === "function")
                return item
            const kids = item.children
            if (!kids)
                continue
            for (let i = 0; i < kids.length; ++i)
                stack.push(kids[i])
        }
        return null
    }

    Timer {
        id: autoStart
        interval: 1200
        running: typeof testAutoAction !== "undefined" && testAutoAction.length > 0
        onTriggered: {
            tio.trace("auto " + testAutoAction + " starting, tabs " + tio.sessions.length)
            window.autoQueue = String(testAutoAction).split(",")
            window.nextAutoStep()
        }
    }

    // The remaining steps of a run: "launch,copy@4000" starts a session and then
    // waits four seconds before copying, which is what a run needs when the bytes
    // it is about to act on arrive from a socket in another process.
    property var autoQueue: []
    function nextAutoStep() {
        if (autoQueue.length === 0)
            return
        const step = String(autoQueue.shift()).trim()
        const at = step.indexOf("@")
        autoStep.name = at >= 0 ? step.substring(0, at) : step
        autoStep.interval = at >= 0 ? Math.max(1, parseInt(step.substring(at + 1), 10)) : 1
        autoStep.restart()
    }

    Timer {
        id: autoStep
        property string name: ""
        onTriggered: {
            window.testAction(name)
            window.nextAutoStep()
        }
    }

    // A port a *running* session holds. It stays selectable — the tab simply
    // records the choice — but the launch refuses it, so the row says why
    // before the click rather than after.
    function portRunningElsewhere(com) {
        const list = typeof tio !== "undefined" ? tio.sessions : []
        for (let i = 0; i < list.length; ++i)
            if (list[i].com === com && list[i].running && !list[i].focused)
                return true
        return false
    }

    // A session can also leave on its own (the user closes the terminal): the
    // only reliable way to notice is to ask the controller again. Ticking only
    // while a session is active keeps an idle window cheap.
    Timer {
        interval: 2000
        repeat: true
        running: typeof tio !== "undefined" && tio.sessionActive
        onTriggered: if (typeof tio !== "undefined") tio.refresh()
    }

    // The shared-socket consumer runs on its own thread; this publishes the new
    // tail without making the socket emit a signal per byte.
    Timer {
        interval: 250
        repeat: true
        running: typeof tio !== "undefined" && tio.sessionSharing
        onTriggered: if (typeof tio !== "undefined") tio.sharePoll()
    }

    // The singleton is read as `DesignTokens.*` throughout.

    // ---- close behaviour ------------------------------------------------------
    // A running session must not disappear with the window: the user picks.
    property bool closing: false
    property string shareConfirmAction: "enable"

    onClosing: function (close) {
        if (!(typeof tio !== "undefined" && tio.sessionActive) || window.closing)
            return
        close.accepted = false
        closeDialog.open()
    }

    Dialog {
        id: closeDialog
        modal: true
        anchors.centerIn: parent
        title: window.tr("还有会话在运行", "A session is still running")
        standardButtons: Dialog.NoButton
        width: 420

        ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: DesignTokens.textColor
                text: window.tr("终端在另一个窗口里。关掉启动器不等于关掉它。",
                                "The terminal is another window. Closing the launcher does not close it.")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                TokenButton {
                    text: window.tr("继续在后台", "Keep running")
                    onClicked: {
                        closeDialog.close()
                        window.hide()
                    }
                }
                TokenButton {
                    text: window.tr("停止会话并关闭", "Stop and close")
                    onClicked: {
                        closeDialog.close()
                        if (typeof tio !== "undefined")
                            tio.stop()
                        window.closing = true
                        window.close()
                    }
                }
                TokenButton {
                    text: window.tr("取消", "Cancel")
                    onClicked: closeDialog.close()
                }
            }
        }
    }


    readonly property int sessionStripHeight: 44

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

    // ---- session tab strip: browser-style; the ＋ creates a session ----------
    // A tab is a place to prepare a session: it may name no port yet, and the
    // port is chosen in the card below. Switching is the only thing a tab click
    // does; closing refuses while the session runs, because the tab is the only
    // handle to its process.
    RowLayout {
        id: tabBar
        Layout.fillWidth: true
        Layout.leftMargin: DesignTokens.pad
        Layout.rightMargin: DesignTokens.pad
        Layout.topMargin: 6
        spacing: 4

        Repeater {
            model: typeof tio !== "undefined" ? tio.sessions : []

            delegate: Rectangle {
                required property var modelData

                // One height for every tab: a tab that names its port draws two
                // lines and one without a port draws one, and the strip must not
                // step up and down as tabs are added or focused.
                Layout.preferredHeight: 52
                Layout.preferredWidth: sessionTabColumn.implicitWidth
                    + closeGlyph.implicitWidth + 40
                radius: DesignTokens.smallRadius
                color: modelData.focused ? DesignTokens.cardColor : "transparent"
                border.width: modelData.focused ? 1 : 0
                border.color: DesignTokens.borderColor

                // The active underline, the way a browser tab marks itself.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 2
                    visible: modelData.focused
                    color: DesignTokens.accentColor
                }

                // The tab-wide click area is declared FIRST, under the content:
                // the ✕ below keeps its own clicks. The tab's words are drawn
                // with Text, not Label — a Control would take the press itself
                // and the tab would only answer to clicks on its blank margins.
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (!modelData.focused) tio.focusSession(modelData.index)
                }

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 8

                    Rectangle {
                        id: sessionDot
                        width: 7
                        height: 7
                        radius: 3.5
                        color: modelData.phase === "running" ? DesignTokens.okColor
                             : modelData.phase === "stopping" ? DesignTokens.warnColor
                             : DesignTokens.faintColor
                    }

                    ColumnLayout {
                        id: sessionTabColumn
                        spacing: 0

                        RowLayout {
                            spacing: 6
                            // A tab with no port says so instead of "COM0": the
                            // number is not a port the user chose.
                            Text {
                                text: modelData.com > 0
                                      ? "COM" + modelData.com
                                      : window.tr("未指定", "no port")
                                color: DesignTokens.textColor
                                font.pixelSize: DesignTokens.fontCaption
                                font.weight: modelData.focused ? Font.DemiBold : Font.Normal
                            }
                            // The close glyph owns a small Item of its own: its
                            // MouseArea is a sibling of the Text, not a child of
                            // it, so the hit area is the Item's box and does not
                            // depend on how the text lays its children out.
                            Item {
                                id: closeButton
                                Layout.preferredWidth: closeGlyph.implicitWidth + 8
                                Layout.preferredHeight: closeGlyph.implicitHeight + 8

                                Text {
                                    id: closeGlyph
                                    anchors.centerIn: parent
                                    text: "✕"
                                    color: DesignTokens.mutedColor
                                    font.pixelSize: DesignTokens.fontSmall
                                }

                                MouseArea {
                                    id: closeArea
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (modelData.running || modelData.busy)
                                            footer.show(window.tr(
                                                "先停止会话，再关闭这个页签",
                                                "Stop the session before closing its tab"))
                                        else
                                            tio.closeSession(modelData.index)
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.maximumWidth: 150
                            visible: text.length > 0
                            text: modelData.name
                            color: DesignTokens.mutedColor
                            font.pixelSize: DesignTokens.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // The ＋: one click creates a session, no port asked for. The port is
        // chosen where it is shown, in the card below.
        Item {
            id: plusItem
            Layout.preferredWidth: plusGlyph.implicitWidth + 16
            Layout.preferredHeight: sessionStripHeight

            Label {
                id: plusGlyph
                anchors.centerIn: parent
                text: "＋"
                color: DesignTokens.mutedColor
                font.pixelSize: DesignTokens.fontBody

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tio.newSession()
                }
            }
        }
    }

        // ---- the scrolling body --------------------------------------------------
        ScrollView {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            // The padding belongs to the content: a ScrollView positions its
            // content rather than anchoring it, so margins there would be ignored.
            ColumnLayout {
                x: DesignTokens.pad
                y: DesignTokens.pad
                width: scroller.availableWidth - DesignTokens.pad * 2
                spacing: DesignTokens.gap

                // ---- session ----
                GroupCard {
                    id: sessionCard
                    title: window.tr("会话", "Session")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(sessionCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(sessionCard.title, folded) }
                    note: typeof tio !== "undefined" && tio.sessionActive
                          ? window.tr("进程在跑；端口状态未知直到 tio 自己给出证据", "the process runs; the port state is unknown until tio proves it")
                          : ""

                    // The session's own actions, right in the title row: the
                    // state below is what they act on, never a separate
                    // button strip drifting under the card.
                    headerExtra: [
                        TokenButton {
                            text: window.tr("保存配置", "Save")
                            enabled: typeof tio !== "undefined" && tio.dirty
                            onClicked: tio.save()
                        },
                        TokenButton {
                            text: window.tr("启动", "Launch")
                            enabled: typeof tio !== "undefined" && tio.canLaunch && !tio.sessionActive
                            onClicked: {
                                if (tio.shareEnabled) {
                                    window.shareConfirmAction = "launch"
                                    shareConfirm.open()
                                } else {
                                    tio.launch()
                                }
                            }
                        },
                        TokenButton {
                            text: window.tr("停止", "Stop")
                            enabled: typeof tio !== "undefined" && tio.sessionActive
                            onClicked: tio.stop()
                        },
                        TokenButton {
                            text: window.tr("重启", "Restart")
                            enabled: typeof tio !== "undefined" && tio.sessionActive
                            onClicked: {
                                if (tio.sessionSharing) {
                                    window.shareConfirmAction = "restart"
                                    shareConfirm.open()
                                } else {
                                    tio.restart(false)
                                }
                            }
                        },
                        TokenButton {
                            text: window.tr("克隆", "Clone")
                            // A clone copies the parameters, not the port: two
                            // idle tabs may name one port, so there is nothing
                            // to run out of.
                            enabled: typeof tio !== "undefined" && tio.sessions.length > 0
                            // The model resolves which tab is focused: reading
                            // tio.sessions here and looping over it walks a live
                            // view, and the clone inside the loop grows the list
                            // it is walking (the tab strip never stopped).
                            onClicked: tio.cloneFocusedSession()
                        }
                    ]

                    GridLayout {
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 4
                        visible: typeof tio !== "undefined" && tio.sessionActive
                        Label { text: window.tr("阶段", "Phase"); color: DesignTokens.mutedColor; font.pixelSize: DesignTokens.fontSmall }
                        Label { text: typeof tio !== "undefined" ? tio.sessionPhase : ""
                            color: DesignTokens.textColor; font.pixelSize: DesignTokens.fontCaption }
                        Label { text: window.tr("端口状态", "Port state"); color: DesignTokens.mutedColor; font.pixelSize: DesignTokens.fontSmall }
                        Label { text: typeof tio !== "undefined" ? tio.sessionPortState : ""
                            color: DesignTokens.warnColor; font.pixelSize: DesignTokens.fontCaption }
                        Label { text: "PID"; color: DesignTokens.mutedColor; font.pixelSize: DesignTokens.fontSmall }
                        Label { text: typeof tio !== "undefined" ? tio.sessionPid : ""
                            color: DesignTokens.textColor; font.pixelSize: DesignTokens.fontCaption }
                        Label { text: window.tr("会话 ID", "Session id"); color: DesignTokens.mutedColor; font.pixelSize: DesignTokens.fontSmall }
                        Label { text: typeof tio !== "undefined" ? tio.sessionToken : ""
                            color: DesignTokens.textColor; font.pixelSize: DesignTokens.fontCaption }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: DesignTokens.mutedColor
                        font.pixelSize: DesignTokens.fontSmall
                        text: window.tr(
                            "“已启动”不等于“端口已打开”：端口状态只有在上游给出可验证信号时才会变化。启用远程共享后，WT 与远程客户端共用同一串口会话。",
                            "\"Started\" is not \"port open\": the port state only changes when the upstream gives a verifiable signal. With remote sharing, WT and remote clients share one serial session.")
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        implicitHeight: 1
                        color: DesignTokens.borderColor
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                width: 7
                                height: 7
                                radius: 3.5
                                color: typeof tio !== "undefined" && tio.shareEnabled
                                       ? DesignTokens.okColor : DesignTokens.faintColor
                            }
                            Label {
                                text: window.tr("高级 · 双向串口共享", "Advanced · bidirectional serial sharing")
                                color: DesignTokens.mutedColor
                                font.pixelSize: DesignTokens.fontSmall
                            }
                            Item { Layout.fillWidth: true }
                            Switch {
                                id: shareSwitch
                                checked: typeof tio !== "undefined" && tio.shareEnabled
                                enabled: typeof tio !== "undefined" && !tio.sessionActive
                                onClicked: {
                                    if (typeof tio === "undefined")
                                        return
                                    if (checked) {
                                        window.shareConfirmAction = "enable"
                                        shareConfirm.open()
                                    } else {
                                        tio.setShareEnabled(false)
                                    }
                                }
                            }
                        }

                        Connections {
                            target: typeof tio !== "undefined" ? tio : null
                            function onChanged() {
                                if (typeof tio !== "undefined")
                                    shareSwitch.checked = tio.shareEnabled
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                text: window.tr("远程端口", "Remote port")
                                color: DesignTokens.textColor
                                font.pixelSize: DesignTokens.fontCaption
                            }
                            SpinBox {
                                id: sharePortInput
                                from: 1024
                                to: 65535
                                editable: true
                                value: typeof tio !== "undefined" ? tio.sharePort : 54321
                                enabled: typeof tio !== "undefined" && !tio.sessionActive
                                // Commit on Enter, arrows and focus loss; a
                                // refused port is written back into the box.
                                onValueModified: if (typeof tio !== "undefined") {
                                    if (!tio.setSharePort(value))
                                        value = tio.sharePort
                                }
                            }
                            Item { Layout.fillWidth: true }
                            TokenButton {
                                text: window.tr("复制端口", "Copy port")
                                enabled: typeof tio !== "undefined" && (tio.shareEnabled || tio.sessionSharing)
                                onClicked: tio.copySharePort()
                            }
                        }

                        // The window is also a local consumer: tio mirrors the
                        // serial session to its socket, and this client shows the
                        // tail. It is its own card below, not a box inside the
                        // settings: reading the console and deciding who may reach
                        // it are two different jobs.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                text: window.tr("写入", "Write")
                                color: DesignTokens.mutedColor
                                font.pixelSize: DesignTokens.fontSmall
                            }
                            Switch {
                                checked: typeof tio !== "undefined" && !tio.shareReadOnly
                                enabled: typeof tio !== "undefined" && tio.shareConnected
                                onToggled: if (typeof tio !== "undefined")
                                               tio.shareReadOnly = !checked
                            }
                            Label {
                                text: window.tr("记日志", "Log")
                                color: DesignTokens.mutedColor
                                font.pixelSize: DesignTokens.fontSmall
                            }
                            Switch {
                                checked: typeof tio !== "undefined" && tio.shareLogEnabled
                                onToggled: if (typeof tio !== "undefined")
                                               tio.setShareLog(checked)
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: typeof tio !== "undefined" && tio.shareLogPath.length > 0
                                      ? window.tr("日志：" + tio.shareLogPath.split(/[\\/]/).pop(),
                                                  "log: " + tio.shareLogPath.split(/[\\/]/).pop())
                                      : ""
                                color: DesignTokens.faintColor
                                font.pixelSize: DesignTokens.fontSmall
                                elide: Text.ElideMiddle
                                Layout.maximumWidth: 300
                            }
                            // Logs pile up in dataDir, which nobody would find by
                            // hand. This is the entry point: one click lists what
                            // is there and opens any of them.
                            TokenButton {
                                text: window.tr("日志历史", "Logs")
                                onClicked: logHistory.open()
                            }
                        }

                        Dialog {
                            id: logHistory
                            modal: true
                            anchors.centerIn: parent
                            width: 460
                            title: window.tr("串口日志", "Serial logs")
                            standardButtons: Dialog.Close

                            // Read when the dialog opens, and again whenever the
                            // model changes: a session still running keeps writing
                            // logs, and a list frozen at open time would say "none"
                            // while the file it is looking for is right there.
                            property var logs: []
                            onOpened: if (typeof tio !== "undefined") logs = tio.shareLogs()

                            Connections {
                                target: typeof tio !== "undefined" ? tio : null
                                function onChanged() {
                                    if (logHistory.visible && typeof tio !== "undefined")
                                        logHistory.logs = tio.shareLogs()
                                }
                            }

                            ColumnLayout {
                                width: parent.width
                                spacing: 8

                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: DesignTokens.mutedColor
                                    font.pixelSize: DesignTokens.fontSmall
                                    text: typeof tio !== "undefined" && tio.shareLogDir.length > 0
                                          ? window.tr("存放在 " + tio.shareLogDir,
                                                      "stored in " + tio.shareLogDir)
                                          : window.tr("还没有日志：启动一次会话并收到字节后才会生成。",
                                                      "no logs yet: one is written once a session receives bytes.")
                                }

                                ListView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 220
                                    clip: true
                                    model: logHistory.logs
                                    spacing: 2

                                    delegate: Rectangle {
                                        required property var modelData
                                        width: ListView.view.width
                                        height: 40
                                        radius: DesignTokens.smallRadius
                                        color: rowHover.hovered ? DesignTokens.hoverColor : "transparent"

                                        HoverHandler { id: rowHover }

                                        Column {
                                            anchors.left: parent.left
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 1
                                            Text {
                                                text: modelData.name
                                                color: DesignTokens.textColor
                                                font.pixelSize: DesignTokens.fontCaption
                                            }
                                            Text {
                                                text: modelData.time + " · " + modelData.bytes + " B"
                                                color: DesignTokens.faintColor
                                                font.pixelSize: DesignTokens.fontSmall
                                            }
                                        }

                                        TokenButton {
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: window.tr("打开", "Open")
                                            onClicked: tio.openShareLog(modelData.path)
                                        }
                                    }
                                }

                                TokenButton {
                                    Layout.alignment: Qt.AlignRight
                                    text: window.tr("打开文件夹", "Open folder")
                                    enabled: typeof tio !== "undefined" && tio.shareLogDir.length > 0
                                    onClicked: tio.openShareLogDir()
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: typeof tio !== "undefined" && (tio.shareEnabled || tio.sessionSharing)
                                   ? DesignTokens.warnColor : DesignTokens.mutedColor
                            font.pixelSize: DesignTokens.fontSmall
                            text: typeof tio !== "undefined" && tio.sessionSharing
                                  ? window.tr("本次会话：所有网卡端口 " + tio.sessionSharePort
                                      + "；客户端可向串口写入。连接地址是本机局域网 IP + 端口。",
                                      "This session listens on all interfaces at port " + tio.sessionSharePort
                                      + ". Clients can write to the serial device; connect to this PC's LAN IP and port.")
                                   : window.tr("远程连接使用：本机局域网 IP + 此端口。启用后 tio 会在所有网卡监听；客户端可向串口写入数据，WT 与远程客户端共用同一会话。慢客户端可能拖住 tio；Windows 防火墙仍可能拦截连接。",
                                       "Remote endpoint: this PC's LAN IP plus this port. When enabled, tio listens on all interfaces; clients can write to the serial device, sharing the session with WT. A slow client may stall tio; Windows Firewall may still block inbound connections.")
                        }
                    }

                    Dialog {
                        id: shareConfirm
                        modal: true
                        anchors.centerIn: parent
                        title: window.tr("确认开放远程串口操作", "Confirm remote serial access")
                        standardButtons: Dialog.Ok | Dialog.Cancel
                        onAccepted: {
                            if (typeof tio === "undefined")
                                return
                            if (window.shareConfirmAction === "restart") {
                                tio.restart(true)
                            } else if (window.shareConfirmAction === "launch") {
                                tio.launch(true)
                            } else {
                                tio.setShareEnabled(true)
                            }
                            window.shareConfirmAction = "enable"
                        }
                        onRejected: {
                            if (typeof tio !== "undefined") {
                                shareSwitch.checked = tio.shareEnabled
                            }
                            window.shareConfirmAction = "enable"
                        }

                        Label {
                            width: 400
                            wrapMode: Text.WordWrap
                            color: DesignTokens.textColor
                            text: window.tr(
                                "tio 将在所有网卡监听端口 " + (typeof tio !== "undefined" ? tio.sharePort : 54321)
                                + "。局域网中的客户端可以向串口写入任意字节。WT 键盘与远程客户端共用此会话；慢客户端可能卡住 tio。仅在可信网络中启用。",
                                "tio will listen on all network interfaces at port "
                                + (typeof tio !== "undefined" ? tio.sharePort : 54321)
                                + ". LAN clients can write arbitrary bytes to the serial device. WT input and remote clients share this session; a slow client may stall tio. WinShun adds no firewall rule. Enable only on a trusted network.")
                        }
                    }
                }

                // ---- the shared serial content, as its own card ----
                // A read-only terminal: tio mirrors the session to its socket and
                // this client shows the tail. Reading the console and deciding who
                // may reach it are different jobs, so this is not a box inside the
                // settings card above.
                GroupCard {
                    id: shareContentCard
                    title: window.tr("共享串口内容", "Shared serial content")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(shareContentCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(shareContentCard.title, folded) }
                    note: typeof tio === "undefined" ? ""
                          : tio.shareConnected
                            ? window.tr("已连接 · 只读视图", "connected · read-only view")
                            : tio.sessionSharing
                              ? window.tr("连接中…", "connecting…")
                              : window.tr("未连接", "not connected")
                    warn: typeof tio !== "undefined" && tio.sessionSharing && !tio.shareConnected

                    headerExtra: [
                        TokenSegmented {
                            Layout.preferredWidth: 110
                            model: ["ascii", "hex"]
                            value: typeof tio !== "undefined" ? tio.shareFormat : "ascii"
                            onPicked: function (choice) { tio.shareFormat = choice }
                        },
                        TokenButton {
                            objectName: "shareCopy"
                            text: window.tr("复制", "Copy")
                            enabled: typeof tio !== "undefined" && tio.shareBytes > 0
                            onClicked: tio.copyShareContent()
                        },
                        TokenButton {
                            text: window.tr("另存为", "Save as")
                            enabled: typeof tio !== "undefined" && tio.shareBytes > 0
                            onClicked: tio.saveShareContent()
                        },
                        TokenButton {
                            text: window.tr("清空", "Clear")
                            enabled: typeof tio !== "undefined" && tio.shareBytes > 0
                            onClicked: tio.clearShareContent()
                        }
                    ]

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: typeof tio !== "undefined"
                                  ? window.tr("已收到 " + tio.shareBytes + " 字节",
                                              tio.shareBytes + " bytes received")
                                  : ""
                            color: DesignTokens.faintColor
                            font.pixelSize: DesignTokens.fontSmall
                        }
                        Item { Layout.fillWidth: true }

                        // The hex view's shape: how many bytes on a line, and
                        // whether the decode column is drawn. Only meaningful in
                        // hex mode, so the row stays quiet in ascii mode.
                        Label {
                            visible: typeof tio !== "undefined" && tio.shareFormat === "hex"
                            text: window.tr("每行", "Per row")
                            color: DesignTokens.mutedColor
                            font.pixelSize: DesignTokens.fontSmall
                        }
                        TokenSegmented {
                            visible: typeof tio !== "undefined" && tio.shareFormat === "hex"
                            Layout.preferredWidth: 132
                            model: ["8", "16", "32"]
                            value: typeof tio !== "undefined" ? String(tio.shareHexRow) : "16"
                            onPicked: function (choice) {
                                if (typeof tio !== "undefined")
                                    tio.shareHexRow = parseInt(choice, 10)
                            }
                        }
                        TokenButton {
                            visible: typeof tio !== "undefined" && tio.shareFormat === "hex"
                            text: window.tr("解析", "Decode")
                            checkable: true
                            checked: typeof tio !== "undefined" && tio.shareHexAscii
                            highlighted: checked
                            onClicked: if (typeof tio !== "undefined")
                                           tio.shareHexAscii = checked
                        }
                        Label {
                            visible: typeof tio === "undefined" || tio.shareFormat !== "hex"
                            text: window.tr("终端 · 保留颜色", "terminal · colours kept")
                            color: DesignTokens.faintColor
                            font.pixelSize: DesignTokens.fontSmall
                        }
                    }

                    // The terminal. The wheel is handled here and always
                    // consumed: with the pointer over this rectangle the wheel
                    // only ever moves this view, and reaching the top or the
                    // bottom does not scroll the page underneath.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 260
                        radius: DesignTokens.smallRadius
                        color: DesignTokens.sunkenColor
                        border.width: 1
                        border.color: DesignTokens.borderColor
                        clip: true

                        Flickable {
                            id: shareTerminal
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 14
                            visible: typeof tio === "undefined" || tio.shareFormat !== "hex"
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: width
                            contentHeight: shareLinesColumn.height

                            // Follow the newest line, but only while the reader is
                            // already at the bottom: scrolling back must not be
                            // undone by the next byte that arrives.
                            property bool following: true
                            onMovementEnded: following = contentY + height >= contentHeight - 2
                            onContentHeightChanged: {
                                if (following)
                                    contentY = Math.max(0, contentHeight - height)
                            }

                            WheelHandler {
                                onWheel: function (event) {
                                    const step = event.angleDelta.y !== 0
                                                 ? -event.angleDelta.y / 2
                                                 : -event.pixelDelta.y
                                    if (shareTerminal.contentHeight > shareTerminal.height) {
                                        shareTerminal.contentY = Math.max(0,
                                            Math.min(shareTerminal.contentHeight - shareTerminal.height,
                                                     shareTerminal.contentY + step))
                                        shareTerminal.following = shareTerminal.contentY
                                            + shareTerminal.height >= shareTerminal.contentHeight - 2
                                    }
                                    // Never bubble to the page behind this card.
                                    event.accepted = true
                                }
                            }

                            Column {
                                id: shareLinesColumn
                                width: shareTerminal.width
                                spacing: 0

                                Repeater {
                                    model: typeof tio !== "undefined" ? tio.shareLines : []

                                    // One row per screen line. Colour, bold,
                                    // inverse and background come from the reducer:
                                    // an escape sequence never reaches this text.
                                    //
                                    // RichText, not StyledText: StyledText silently
                                    // ignores `<span style="color:…">` and every
                                    // console colour came out black. RichText collapses
                                    // runs of spaces, so a space is written as &nbsp;
                                    // and the columns stay aligned.
                                    delegate: Text {
                                        required property var modelData
                                        width: shareLinesColumn.width
                                        textFormat: Text.RichText
                                        wrapMode: Text.NoWrap
                                        font.family: "Consolas"
                                        font.pixelSize: DesignTokens.fontSmall
                                        color: DesignTokens.textColor
                                        text: {
                                            let out = ""
                                            const spans = modelData.spans
                                            for (let i = 0; i < spans.length; ++i) {
                                                const span = spans[i]
                                                const fg = span.inverse
                                                    ? (span.background && String(span.background).length > 0
                                                        ? span.background : String(DesignTokens.sunkenColor))
                                                    : span.color
                                                const bg = span.inverse
                                                    ? (span.color && String(span.color).length > 0
                                                        ? span.color : String(DesignTokens.textColor))
                                                    : span.background
                                                let style = ""
                                                if (fg && String(fg).length > 0)
                                                    style += "color:" + fg + ";"
                                                if (bg && String(bg).length > 0)
                                                    style += "background-color:" + bg + ";"
                                                if (span.bold)
                                                    style += "font-weight:600;"
                                                const safe = String(span.text)
                                                    .replace(/&/g, "&amp;")
                                                    .replace(/</g, "&lt;")
                                                    .replace(/>/g, "&gt;")
                                                    .replace(/ /g, "&nbsp;")
                                                out += style.length > 0
                                                    ? "<span style=\"" + style + "\">" + safe + "</span>"
                                                    : safe
                                            }
                                            return out.length > 0 ? out : "&nbsp;"
                                        }
                                    }
                                }
                            }
                        }

                        // A visible scrollbar: a long console with no handle is
                        // indistinguishable from a console that has stopped, and
                        // there is no other way to tell how much is off-screen.
                        ScrollBar {
                            id: shareScrollBar
                            orientation: Qt.Vertical
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            policy: ScrollBar.AlwaysOn
                            active: true
                            size: shareTerminal.height
                                  / Math.max(1, shareTerminal.contentHeight)
                            position: shareTerminal.contentHeight > shareTerminal.height
                                      ? shareTerminal.contentY
                                        / Math.max(1, shareTerminal.contentHeight - shareTerminal.height)
                                      : 0

                            // Dragging the handle must move the terminal; the
                            // terminal moving must move the handle. A two-way
                            // binding would make the two fight, so only the
                            // reader's drag writes back here.
                            onPositionChanged: {
                                if (!pressed)
                                    return
                                shareTerminal.following = false
                                shareTerminal.contentY = position
                                    * Math.max(0, shareTerminal.contentHeight - shareTerminal.height)
                            }
                        }

                        // HEX: the same bytes as a 16-per-line dump, so a device
                        // sending binary is readable at all.
                        ScrollView {
                            anchors.fill: parent
                            visible: typeof tio !== "undefined" && tio.shareFormat === "hex"
                            clip: true
                            ScrollBar.vertical.policy: ScrollBar.AlwaysOn

                            TextArea {
                                readOnly: true
                                selectByMouse: true
                                text: typeof tio !== "undefined" ? tio.shareContent : ""
                                wrapMode: TextArea.NoWrap
                                color: DesignTokens.textColor
                                font.family: "Consolas"
                                font.pixelSize: DesignTokens.fontSmall
                                background: null

                                WheelHandler {
                                    onWheel: function (event) { event.accepted = true }
                                }
                            }
                        }

                        Text {
                            anchors.fill: parent
                            anchors.margins: 12
                            visible: typeof tio === "undefined" || tio.shareBytes === 0
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: DesignTokens.faintColor
                            font.pixelSize: DesignTokens.fontSmall
                            text: typeof tio !== "undefined" && tio.sessionSharing
                                  ? window.tr("等设备说话…", "waiting for the device to speak…")
                                  : window.tr("启动会话后，这里显示本次共享串口收到的内容。",
                                              "once the session runs, this shows what the shared serial session receives.")
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: DesignTokens.faintColor
                        font.pixelSize: DesignTokens.fontSmall
                        text: typeof tio !== "undefined" && tio.shareLogEnabled
                              ? window.tr("收到的原始字节会持续写入 dataDir 下的日志文件。",
                                          "the raw bytes are appended to a log file under dataDir.")
                              : window.tr("没有记录日志；可在上面的共享设置里打开。",
                                          "not logging; turn it on in the sharing settings above.")
                    }
                }

                GroupCard {
                    id: portCard
                    title: window.tr("端口与参数", "Port & parameters")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(portCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(portCard.title, folded) }
                    note: typeof tio !== "undefined" && tio.sessionActive
                          ? window.tr("会话运行中 · 锁定", "session running · locked")
                          : typeof tio !== "undefined" && tio.portText !== "—"
                            ? tio.portText
                            : window.tr("只列 tio 3.9 接受的取值", "only the values tio 3.9 takes")
                    warn: typeof tio !== "undefined" && tio.sessionActive

                    FieldRow {
                        label: window.tr("端口", "Port")
                        hint: typeof tio !== "undefined" && tio.deviceText.length > 0
                              ? tio.deviceText : window.tr("未选择", "none chosen")

                        TokenComboBox {
                            id: portBox
                            Layout.fillWidth: true
                            Layout.preferredHeight: DesignTokens.rowHeight
                            // Parameters are the running session's: it took them
                            // at launch, and nothing here reaches it.
                            enabled: !(typeof tio !== "undefined" && tio.sessionActive)
                            model: typeof tio !== "undefined" ? tio.ports : []
                            // A screenshot run may ask for the list to start
                            // open. It waits for the window to be on screen:
                            // a popup opened while the component is still
                            // completing has no window to attach to.
                            Component.onCompleted: {
                                if (typeof openPortListOnStart !== "undefined" && openPortListOnStart)
                                    openListTimer.start()
                            }
                            Timer {
                                id: openListTimer
                                interval: 400
                                onTriggered: portBox.popup.open()
                            }
                            // What the closed box shows is what Device Manager
                            // shows under Ports (COM & LPT): the friendly name,
                            // which already ends in "(COMn)".
                            textRole: "name"
                            valueRole: "com"
                            currentIndex: {
                                const list = model
                                for (let i = 0; i < list.length; ++i) {
                                    if (list[i].current)
                                        return i
                                }
                                return -1
                            }
                            onActivated: function (index) {
                                const row = model[index]
                                if (row)
                                    tio.selectPort(row.com)
                            }
                            // A tab with no port is not a tab with no ports on
                            // the machine: the two say different things.
                            displayText: currentIndex >= 0
                                         ? currentText
                                         : model.length === 0
                                           ? window.tr("（无可用端口）", "(no port)")
                                           : window.tr("未指定", "unspecified")
                            delegate: ItemDelegate {
                                id: portOption
                                required property var modelData
                                required property int index
                                width: portBox.width
                                // Two lines (the Device Manager name and the
                                // device path): the entry is as tall as what it
                                // shows, not as tall as a single-line row.
                                height: Math.max(DesignTokens.rowHeight,
                                                 portOption.contentItem.implicitHeight + 14)
                                highlighted: portBox.highlightedIndex === index
                                contentItem: ColumnLayout {
                                    spacing: 3
                                    Label {
                                        // The line Device Manager shows, plus where
                                        // the port sits in tio's numbering.
                                        text: modelData.name
                                        color: DesignTokens.textColor
                                        font.pixelSize: DesignTokens.fontCaption
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: modelData.device + " · " + modelData.identity
                                        color: DesignTokens.mutedColor
                                        font.pixelSize: DesignTokens.fontSmall
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    // Selectable, but the launch will say no:
                                    // "one port, one process" is the rule, and
                                    // the row names it here.
                                    Label {
                                        visible: window.portRunningElsewhere(modelData.com)
                                        text: window.tr("这个端口已经在另一个会话里运行",
                                                        "this port already runs in another session")
                                        color: DesignTokens.warnColor
                                        font.pixelSize: DesignTokens.fontSmall
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }

                        TokenButton {
                            text: window.tr("刷新", "Refresh")
                            Layout.preferredHeight: DesignTokens.rowHeight
                            onClicked: tio.refresh()
                        }
                    }

                    // Where to pick a port, said once, where the box is.
                    Label {
                        Layout.fillWidth: true
                        visible: !window.hasPort
                        wrapMode: Text.WordWrap
                        color: DesignTokens.warnColor
                        font.pixelSize: DesignTokens.fontSmall
                        text: window.tr("先在端口与参数里选择一个串口：启动需要它。",
                                        "pick a port here first: the launch needs one.")
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        implicitHeight: 1
                        color: DesignTokens.borderColor
                    }

                    FieldRow {
                        label: window.tr("波特率", "Baud rate")
                        TokenComboBox {
                            Layout.fillWidth: true
                            Layout.preferredHeight: DesignTokens.rowHeight
                            enabled: !(typeof tio !== "undefined" && tio.sessionActive)
                            model: typeof tio !== "undefined" ? tio.baudrateOptions : []
                            currentIndex: typeof tio !== "undefined"
                                          ? Math.max(0, model.indexOf(tio.baudrate)) : 0
                            onActivated: function (index) { tio.setBaudrate(model[index]) }
                        }
                    }
                    // A few values each: the host's plugin pages show choices like
                    // these side by side on a segmented track (TokenSegmented),
                    // not one dropdown per row. The baud rate keeps its dropdown:
                    // it lists every rate tio 3.9 takes, too many for a row.
                    FieldRow {
                        label: window.tr("数据位", "Data bits")
                        TokenSegmented {
                            Layout.fillWidth: true
                            enabled: !(typeof tio !== "undefined" && tio.sessionActive)
                            model: typeof tio !== "undefined" ? tio.dataBitsOptions : []
                            value: typeof tio !== "undefined" ? tio.dataBits : ""
                            onPicked: function (choice) { tio.setDataBits(choice) }
                        }
                    }
                    FieldRow {
                        label: window.tr("校验", "Parity")
                        TokenSegmented {
                            Layout.fillWidth: true
                            enabled: !(typeof tio !== "undefined" && tio.sessionActive)
                            model: typeof tio !== "undefined" ? tio.parityOptions : []
                            value: typeof tio !== "undefined" ? tio.parity : ""
                            onPicked: function (choice) { tio.setParity(choice) }
                        }
                    }
                    FieldRow {
                        label: window.tr("停止位", "Stop bits")
                        TokenSegmented {
                            Layout.fillWidth: true
                            enabled: !(typeof tio !== "undefined" && tio.sessionActive)
                            model: typeof tio !== "undefined" ? tio.stopBitsOptions : []
                            value: typeof tio !== "undefined" ? tio.stopBits : ""
                            onPicked: function (choice) { tio.setStopBits(choice) }
                        }
                    }
                    FieldRow {
                        label: window.tr("流控", "Flow control")
                        hint: window.tr("hard 时 RTS 由流控管理，不承诺固定电平",
                                        "with hard flow the RTS follows the flow, not a fixed level")
                        TokenSegmented {
                            Layout.fillWidth: true
                            enabled: !(typeof tio !== "undefined" && tio.sessionActive)
                            model: typeof tio !== "undefined" ? tio.flowOptions : []
                            value: typeof tio !== "undefined" ? tio.flow : ""
                            onPicked: function (choice) { tio.setFlow(choice) }
                        }
                    }
                }

                // The combobox already names the device the way Device Manager
                // does; this card holds only what that line does not say. It
                // starts folded: capture is automatic (picking a port re-reads
                // the identity), the fold is about reading the details, never
                // about triggering them (TIO-010).
                GroupCard {
                    id: detailsCard
                    title: window.tr("设备详情", "Device details")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(detailsCard.title, true)
                    onToggled: function (folded) { tio.setCardCollapsed(detailsCard.title, folded) }
                    note: typeof tio !== "undefined" && !window.hasPort
                          ? window.tr("未选择串口：没有可确认的身份",
                                      "no port chosen: no identity to confirm")
                          : typeof tio !== "undefined" && tio.identityValid
                            ? window.tr("采集于 ", "captured at ") + tio.identityTime
                            : window.tr("身份未知：只能确认当前端口", "unknown: only the current port is confirmed")
                    warn: !(typeof tio !== "undefined" && window.hasPort && tio.identityValid)

                    // One row per fact, and a row only for a fact the
                    // device actually reports.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        DetailLine {
                            label: window.tr("厂商", "Manufacturer")
                            value: typeof tio !== "undefined" ? tio.identityManufacturer : ""
                        }
                        DetailLine {
                            label: window.tr("产品", "Product")
                            value: typeof tio !== "undefined" ? tio.identityChip : ""
                        }
                        DetailLine {
                            label: window.tr("VID/PID", "VID/PID")
                            value: typeof tio !== "undefined"
                                  && (tio.identityVendorId.length > 0 || tio.identityProductId.length > 0)
                                  ? tio.identityVendorId + " / " + tio.identityProductId : ""
                            mono: true
                        }
                        DetailLine {
                            label: window.tr("序列号", "Serial number")
                            value: typeof tio !== "undefined" ? tio.identitySerial : ""
                            mono: true
                        }
                        DetailLine {
                            label: window.tr("总线", "Bus")
                            value: typeof tio !== "undefined" ? tio.identityBus : ""
                        }
                        DetailLine {
                            label: window.tr("驱动", "Driver")
                            value: typeof tio !== "undefined" ? tio.identityDriver : ""
                            mono: true
                        }
                        DetailLine {
                            label: window.tr("位置", "Location")
                            value: typeof tio !== "undefined" ? tio.identityLocation : ""
                        }
                        DetailLine {
                            label: window.tr("实例 ID", "Instance id")
                            value: typeof tio !== "undefined" ? tio.identity : ""
                            mono: true
                        }
                        DetailLine {
                            label: window.tr("设备状态", "Device status")
                            value: typeof tio !== "undefined" && tio.identityProblem.length > 0
                                  ? window.tr("Windows 报告问题码 ", "Windows reports problem code ")
                                    + tio.identityProblem
                                  : window.tr("正常", "no problem reported")
                            warn: typeof tio !== "undefined" && tio.identityProblem.length > 0
                        }
                    }
                }

                // ---- backend ----
                GroupCard {
                    id: backendCard
                    title: window.tr("终端后端", "Terminal backend")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(backendCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(backendCard.title, folded) }
                    note: typeof tio !== "undefined" ? tio.backendText : ""

                    Repeater {
                        model: typeof tio !== "undefined" ? tio.backends : []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: rowLayout.implicitHeight + 12
                            radius: DesignTokens.radius
                            color: modelData.current ? DesignTokens.sunkenColor : "transparent"
                            border.width: modelData.current ? 1 : 0
                            border.color: DesignTokens.accentColor
                            opacity: modelData.available ? 1 : 0.55

                            RowLayout {
                                id: rowLayout
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                RadioButton {
                                    checked: modelData.current
                                    enabled: modelData.available
                                    onClicked: tio.setBackend(modelData.id)
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        text: modelData.title
                                             + (modelData.version.length > 0 ? " " + modelData.version : "")
                                        color: DesignTokens.textColor
                                        font.pixelSize: DesignTokens.fontCaption
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: DesignTokens.mutedColor
                                        font.pixelSize: DesignTokens.fontSmall
                                        text: !modelData.available
                                              ? window.tr("这台机器没有：不能选", "not on this machine: not selectable")
                                              : modelData.owned
                                                ? window.tr("tio 是我们创建的子进程：停止可靠", "tio is the child process we create: Stop is reliable")
                                                : window.tr("tio 由 Windows Terminal 创建：停止靠标题与创建时间定位，归属受限",
                                                            "Windows Terminal creates tio: Stop finds it by title and creation time, ownership is limited")
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                }

                // ---- package ----
                // The integrity card carries the third-party record itself: what
                // each component is, where it came from, which licence text
                // ships with it and why it is (or is not) inside the package.
                // Nothing here opens a text file to be believed — the links are
                // there to check, not to be the only way to read.
                GroupCard {
                    id: packageCard
                    title: window.tr("包完整性", "Package integrity")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(packageCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(packageCard.title, folded) }
                    note: typeof tio !== "undefined" && tio.packageValid
                          ? window.tr("有效 · 每个文件都与清单一致",
                                      "valid · every file matches the manifest")
                          : window.tr("校验失败，不能启动", "the check failed: launch is refused")
                    warn: !(typeof tio !== "undefined" && tio.packageValid)

                    // The summary line stays outside the fold: the reader wants
                    // the size and the file count before deciding to unfold.
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: DesignTokens.mutedColor
                        font.pixelSize: DesignTokens.fontSmall
                        text: {
                            if (typeof tio === "undefined")
                                return ""
                            const version = tio.packageVersion.length > 0 ? tio.packageVersion : "—"
                            return window.tr("tio " + version + " · "
                                    + tio.packageFileCount + " 个运行时文件 · " + tio.packageSize
                                    + " —— 展开看三方文件与来源",
                                "tio " + version + " · " + tio.packageFileCount
                                    + " runtime files · " + tio.packageSize
                                    + " — unfold for third-party files and sources")
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        visible: typeof tio !== "undefined" && !tio.packageValid
                        text: typeof tio !== "undefined" ? tio.packageProblem : ""
                        color: DesignTokens.mutedColor
                        font.pixelSize: DesignTokens.fontSmall
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: DesignTokens.mutedColor
                        font.pixelSize: DesignTokens.fontSmall
                        text: window.tr(
                            "包里带的是上游 tio 与它真正导入的 MSYS2 运行时；每行给出来源、许可证和原文，点开可查。",
                            "The package ships upstream tio plus the MSYS2 runtime it really imports; "
                            + "each row links its source, licence and licence text for verification.")
                    }

                    // One row per component: name and version, where it came
                    // from, the licence text that ships with it, and why it is
                    // (or is not) inside the package.
                    Repeater {
                        model: typeof tio !== "undefined" ? tio.thirdParty : []

                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 3

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: DesignTokens.borderColor
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    text: modelData.name
                                    color: DesignTokens.textColor
                                    font.pixelSize: DesignTokens.fontCaption
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    text: modelData.version
                                    color: DesignTokens.mutedColor
                                    font.pixelSize: DesignTokens.fontSmall
                                }
                                Label {
                                    text: modelData.license
                                    color: DesignTokens.mutedColor
                                    font.pixelSize: DesignTokens.fontSmall
                                }
                                Rectangle {
                                    visible: !modelData.bundled
                                    Layout.preferredWidth: notBundledLabel.implicitWidth + 12
                                    Layout.preferredHeight: 16
                                    radius: 8
                                    color: "transparent"
                                    border.width: 1
                                    border.color: DesignTokens.warnColor
                                    Label {
                                        id: notBundledLabel
                                        anchors.centerIn: parent
                                        text: window.tr("不随包", "not bundled")
                                        color: DesignTokens.warnColor
                                        font.pixelSize: DesignTokens.fontSmall
                                    }
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    text: window.tr("来源", "source") + ":"
                                    color: DesignTokens.mutedColor
                                    font.pixelSize: DesignTokens.fontSmall
                                }
                                LinkLabel {
                                    text: modelData.sourceLabel
                                    onClicked: if (typeof tio !== "undefined") tio.openWebLink(modelData.source)
                                }
                                LinkLabel {
                                    visible: modelData.extra.length > 0
                                    text: modelData.extraLabel
                                    onClicked: if (typeof tio !== "undefined") tio.openWebLink(modelData.extra)
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    text: window.tr("许可证原文", "licence text") + ":"
                                    color: DesignTokens.mutedColor
                                    font.pixelSize: DesignTokens.fontSmall
                                }
                                Repeater {
                                    model: modelData.licenseFiles
                                    delegate: LinkLabel {
                                        required property string modelData
                                        text: modelData
                                        // A licence text that ships with the
                                        // package opens the exact copy the
                                        // launcher verified.
                                        onClicked: if (typeof tio !== "undefined")
                                            tio.openPackageDoc(modelData)
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: modelData.why
                                color: DesignTokens.mutedColor
                                font.pixelSize: DesignTokens.fontSmall
                            }
                        }
                    }

                    // The full documents are still one click away, but they are
                    // the appendix now, not the only way to read the record.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: DesignTokens.mutedColor
                            font.pixelSize: DesignTokens.fontSmall
                            text: window.tr("完整文档（随包）：第三方说明与逐文件清单。",
                                            "Full documents (shipped with the package): the third-party notes and the per-file manifest.")
                        }
                        TokenButton {
                            text: window.tr("第三方说明", "Third-party notes")
                            onClicked: if (typeof tio !== "undefined") tio.openPackageDoc("THIRD_PARTY.md")
                        }
                        TokenButton {
                            text: window.tr("文件清单", "File manifest")
                            onClicked: if (typeof tio !== "undefined") tio.openPackageDoc("manifest/files.json")
                        }
                    }
                }

                // ---- diagnostics ----
                GroupCard {
                    id: diagCard
                    title: window.tr("诊断", "Diagnostics")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(diagCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(diagCard.title, folded) }
                    note: window.tr("不含串口内容", "never serial content")

                    // A plain read-only TextArea, not a ScrollView: the report is
                    // short, and a nested scroller would eat the wheel while the
                    // page is the thing that should scroll (TIO-009). The text
                    // wraps, so nothing is ever cut off.
                    TextArea {
                        Layout.fillWidth: true
                        readOnly: true
                        textFormat: TextEdit.PlainText
                        wrapMode: TextArea.Wrap
                        font.family: "Consolas"
                        font.pixelSize: DesignTokens.fontSmall
                        color: DesignTokens.textColor
                        text: typeof tio !== "undefined" ? tio.diagnostics() : ""
                    }
                    TokenButton {
                        text: window.tr("复制诊断", "Copy diagnostics")
                        onClicked: tio.copyDiagnostics()
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ---- the footer bar -----------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: bar.implicitHeight + DesignTokens.pad
            color: DesignTokens.cardColor
            border.width: 1
            border.color: DesignTokens.borderColor

            ColumnLayout {
                id: bar
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                // The footer is status only: the session's Launch/Stop/Restart
                // live in the session card's header, and Save lives beside them
                // there too — actions never sit in two places.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        // While a session runs, "launch blocked: session" would
                        // read as a fault. The state itself is the news.
                        text: typeof tio !== "undefined" && tio.sessionActive
                              ? window.tr("会话运行中", "session running")
                              : typeof tio !== "undefined" && tio.canLaunch
                                ? window.tr("就绪", "Ready")
                                : window.tr("启动被阻塞：", "launch blocked: ")
                                  + (typeof tio !== "undefined" ? tio.blockedReason : "")
                        color: typeof tio !== "undefined" && (tio.canLaunch || tio.sessionActive)
                               ? DesignTokens.mutedColor : DesignTokens.warnColor
                        font.pixelSize: DesignTokens.fontSmall
                        elide: Text.ElideRight
                    }

                    TokenButton {
                        text: window.tr("保存配置", "Save")
                        enabled: typeof tio !== "undefined" && tio.dirty
                        onClicked: tio.save()
                    }
                }

                Label {
                    id: footer
                    Layout.fillWidth: true
                    color: DesignTokens.mutedColor
                    font.pixelSize: DesignTokens.fontSmall
                    elide: Text.ElideRight
                    // The last error, when there is one, replaces the standing
                    // sentence: it is the reason the user needs to read.
                    text: typeof tio !== "undefined" && tio.errorText.length > 0
                          ? tio.errorText
                          : window.tr("插件不读取串口数据。远程共享启用后，网络客户端可向串口写入。",
                                      "The plugin does not read serial data. When remote sharing is enabled, network clients can write to the serial device.")

                    property string base: text
                    function show(text) {
                        footer.text = text
                        hideTimer.restart()
                    }
                    Timer {
                        id: hideTimer
                        interval: 4000
                        onTriggered: footer.text = typeof tio !== "undefined" && tio.errorText.length > 0
                                     ? tio.errorText
                                     : footer.base
                    }
                }
            }
        }
    }
}
