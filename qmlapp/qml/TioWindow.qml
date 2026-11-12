// TioWindow.qml - the standalone window (path B).
//
// The same groups the search box shows, plus what a window can hold and the
// list cannot: a read-only port dropdown named the way Device Manager names its
// ports, a device-details card that captures automatically and folds the fine
// print away, the session block, and Save beside Launch. The pinned tio 3.9 has
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

    // ---- the session's content -----------------------------------------------
    // What the focused session captured, kept here rather than read on every
    // repaint: the view polls while a session runs and stops when it does.
    property string captureBody: ""
    // "—" is what the model says when the tab names no port.
    readonly property bool hasPort: typeof tio !== "undefined" && tio.portText !== "—"

    function refreshCapture() {
        if (typeof tio === "undefined")
            return
        // tio writes a newline when it starts, and a whitespace-only tail would
        // hide the "nothing captured yet" placeholder behind an empty box.
        const tail = tio.captureTail(32768)
        const body = tail.trim().length === 0 ? "" : tail
        if (body !== window.captureBody)
            window.captureBody = body
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

    Timer {
        interval: 1000
        repeat: true
        running: typeof tio !== "undefined" && tio.sessionCapturing
        onTriggered: window.refreshCapture()
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

    // The singleton is read as `DesignTokens.*` throughout.

    // ---- close behaviour ------------------------------------------------------
    // A running session must not disappear with the window: the user picks.
    property bool closing: false

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
                            onClicked: tio.launch()
                        },
                        TokenButton {
                            text: window.tr("停止", "Stop")
                            enabled: typeof tio !== "undefined" && tio.sessionActive
                            onClicked: tio.stop()
                        },
                        TokenButton {
                            text: window.tr("重启", "Restart")
                            enabled: typeof tio !== "undefined" && tio.sessionActive
                            onClicked: tio.restart()
                        },
                        TokenButton {
                            text: window.tr("克隆", "Clone")
                            // A clone copies the parameters, not the port: two
                            // idle tabs may name one port, so there is nothing
                            // to run out of.
                            enabled: typeof tio !== "undefined" && tio.sessions.length > 0
                            onClicked: {
                                const list = tio.sessions
                                for (let i = 0; i < list.length; ++i)
                                    if (list[i].focused)
                                        tio.cloneSession(i)
                            }
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
                            "“已启动”不等于“端口已打开”：端口状态只有在上游给出可验证信号时才会变化。打开捕获时，插件读的是 tio 自己写下的日志，不是串口。",
                            "\"Started\" is not \"port open\": the port state only changes when the upstream gives a "
                            + "verifiable signal. With capture on, the plugin reads the log tio writes — never the port.")
                    }

                    // ---- the session's content ----
                    // A ruled sub-head inside the same card, not a card of its
                    // own: the content belongs to this session, and a session
                    // without a log simply has none.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        implicitHeight: 1
                        color: DesignTokens.borderColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            id: captureDot
                            width: 7
                            height: 7
                            radius: 3.5
                            color: typeof tio !== "undefined" && tio.sessionCapturing
                                   ? DesignTokens.okColor : DesignTokens.faintColor
                        }
                        Label {
                            text: window.tr("会话内容", "Session content")
                            color: DesignTokens.mutedColor
                            font.pixelSize: DesignTokens.fontSmall
                        }
                        Label {
                            text: typeof tio === "undefined" ? ""
                                  : tio.sessionCapturing
                                    ? window.tr("捕获中", "capturing")
                                    : tio.captureEnabled
                                      ? window.tr("已开启 · 启动后记录", "on · records once launched")
                                      : window.tr("未捕获", "not capturing")
                            color: typeof tio !== "undefined" && tio.sessionCapturing
                                   ? DesignTokens.okColor : DesignTokens.faintColor
                            font.pixelSize: DesignTokens.fontSmall
                        }
                        Item { Layout.fillWidth: true }
                        TokenButton {
                            // tio takes --log-file once, when it starts, so this
                            // switch is the next launch's. The notice says so
                            // when a session is already running.
                            text: typeof tio !== "undefined" && tio.captureEnabled
                                  ? window.tr("关闭捕获", "Stop capture")
                                  : window.tr("开始捕获", "Start capture")
                            onClicked: tio.setCapture(!tio.captureEnabled)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: DesignTokens.mutedColor
                        font.pixelSize: DesignTokens.fontSmall
                        text: !window.hasPort
                              ? window.tr("先在端口与参数里选择一个串口", "pick a port in Port & parameters first")
                              : !(typeof tio !== "undefined" && tio.captureEnabled)
                                ? window.tr("打开捕获：启动时让 tio 用它自己的 --log-file 把收发写进本机日志，插件只读这个文件，不碰串口。",
                                            "Turn capture on and the launch lets tio write its own --log-file inside this "
                                            + "machine's dataDir; the plugin only reads that file, never the port.")
                                : !(typeof tio !== "undefined" && tio.sessionActive)
                                  ? window.tr("捕获已打开：启动后开始记录。", "Capture is on: it starts recording once the session runs.")
                                  : window.tr("日志由 tio 写入 dataDir；停止会话后仍然可以复制或另存。",
                                              "tio writes the log into dataDir; after the session stops it can still be copied or saved.")
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 160
                        radius: DesignTokens.radius
                        color: DesignTokens.sunkenColor
                        border.width: 1
                        border.color: DesignTokens.borderColor

                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 6
                            clip: true

                            TextArea {
                                id: captureView
                                readOnly: true
                                selectByMouse: true
                                text: window.captureBody
                                wrapMode: TextArea.NoWrap
                                color: DesignTokens.textColor
                                // Monospace: this is a terminal log, and the
                                // columns are what the user reads.
                                font.family: "Consolas"
                                font.pixelSize: DesignTokens.fontSmall
                                background: null
                                placeholderText: typeof tio !== "undefined" && tio.sessionCapturing
                                                 ? window.tr("等设备说话…", "waiting for the device to speak…")
                                                 : window.tr("这里只有 tio 的日志：打开捕获并启动会话后才有内容。",
                                                             "only the log tio writes appears here: turn capture on and launch.")
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TokenButton {
                            text: window.tr("复制全部", "Copy all")
                            enabled: window.captureBody.length > 0
                            onClicked: tio.copyCapture()
                        }
                        TokenButton {
                            text: window.tr("另存为文本", "Save as text")
                            enabled: window.captureBody.length > 0
                            onClicked: tio.saveCapture()
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            Layout.maximumWidth: 380
                            wrapMode: Text.WordWrap
                            color: DesignTokens.faintColor
                            font.pixelSize: DesignTokens.fontSmall
                            horizontalAlignment: Text.AlignRight
                            text: window.tr("内容只留在本机：不上传、不遥测。删除会话即删日志。",
                                            "It stays on this machine: no upload, no telemetry. Closing the session deletes the log.")
                        }
                    }
                }

                // ---- port and parameters ----
                // One card, because choosing the port and choosing the bytes it
                // is opened with is one decision. A tab may name no port at all:
                // the box then says 未指定 and the hint says where to pick one.
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
                          : window.tr("插件不碰串口：打开捕获时，只读 tio 自己写在本机 dataDir 的日志。",
                                      "The plugin never touches the port; with capture on it reads only the log tio writes under dataDir.")

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
