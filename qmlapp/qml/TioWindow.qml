// TioWindow.qml - the standalone window (path B).
//
// The same five groups the search box shows, plus what a window can hold and the
// list cannot: a read-only port dropdown with a refresh, the identity's capture
// time, a collapsed panel explaining why the control lines cannot be set, the
// session block, and Save beside Launch.
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

    // The device details start closed: the main line says what the device is,
    // the panel opens what identifies it.
    property bool detailsOpen: false

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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

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

                // ---- port ----
                GroupCard {
                    id: portCard
                    title: window.tr("端口", "Port")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(portCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(portCard.title, folded) }
                    note: typeof tio !== "undefined" && tio.portText.length > 0
                          ? tio.portText : window.tr("只读枚举；输入不改端口号", "read-only enumeration")

                    FieldRow {
                        label: window.tr("端口", "Port")
                        hint: typeof tio !== "undefined" && tio.deviceText.length > 0
                              ? tio.deviceText : window.tr("未选择", "none chosen")

                        TokenComboBox {
                            id: portBox
                            Layout.fillWidth: true
                            Layout.preferredHeight: DesignTokens.rowHeight
                            model: typeof tio !== "undefined" ? tio.ports : []
                            textRole: "title"
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
                            displayText: currentIndex >= 0 ? currentText : window.tr("（无可用端口）", "(no port)")
                            delegate: ItemDelegate {
                                required property var modelData
                                width: portBox.width
                                height: DesignTokens.rowHeight
                                contentItem: ColumnLayout {
                                    spacing: 0
                                    Label {
                                        text: modelData.title
                                        color: DesignTokens.textColor
                                        font.pixelSize: DesignTokens.fontCaption
                                    }
                                    Label {
                                        text: modelData.device + " · " + modelData.identity
                                        color: DesignTokens.mutedColor
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

                    // The device says more about itself than the instance id:
                    // the main line shows what it is, the details panel opens
                    // what identifies it (the chip's ids, the serial, the
                    // driver) — the facts a launch decision rests on.
                    FieldRow {
                        label: window.tr("设备身份", "Device identity")
                        hint: typeof tio !== "undefined" && tio.identityValid
                              ? window.tr("采集于 ", "captured at ") + tio.identityTime
                              : window.tr("身份未知：只能确认当前端口", "unknown: only the current port is confirmed")
                        warn: !(typeof tio !== "undefined" && tio.identityValid)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Label {
                                    Layout.fillWidth: true
                                    text: typeof tio !== "undefined" && tio.identityValid
                                          && tio.identityName.length > 0
                                          ? tio.identityName
                                          : (typeof tio !== "undefined" && tio.identityValid
                                             ? tio.identity
                                             : window.tr("（无）", "(none)"))
                                    color: typeof tio !== "undefined" && tio.identityValid
                                           ? DesignTokens.textColor : DesignTokens.mutedColor
                                    font.pixelSize: DesignTokens.fontCaption
                                    elide: Text.ElideRight
                                }

                                TokenButton {
                                    text: detailsOpen
                                          ? window.tr("收起详情", "Hide details")
                                          : window.tr("设备详情", "Device details")
                                    onClicked: detailsOpen = !detailsOpen
                                }
                            }

                            // The details: one row per fact, and a row only for
                            // a fact the device actually reports.
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                visible: detailsOpen

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
                    }
                }

                // ---- serial parameters ----
                GroupCard {
                    id: serialCard
                    title: window.tr("串口参数", "Serial parameters")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(serialCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(serialCard.title, folded) }
                    note: window.tr("只列 tio 3.9 接受的取值", "only the values tio 3.9 takes")

                    FieldRow {
                        label: window.tr("波特率", "Baud rate")
                        TokenComboBox {
                            Layout.fillWidth: true
                            Layout.preferredHeight: DesignTokens.rowHeight
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
                            model: typeof tio !== "undefined" ? tio.dataBitsOptions : []
                            value: typeof tio !== "undefined" ? tio.dataBits : ""
                            onPicked: function (choice) { tio.setDataBits(choice) }
                        }
                    }
                    FieldRow {
                        label: window.tr("校验", "Parity")
                        TokenSegmented {
                            Layout.fillWidth: true
                            model: typeof tio !== "undefined" ? tio.parityOptions : []
                            value: typeof tio !== "undefined" ? tio.parity : ""
                            onPicked: function (choice) { tio.setParity(choice) }
                        }
                    }
                    FieldRow {
                        label: window.tr("停止位", "Stop bits")
                        TokenSegmented {
                            Layout.fillWidth: true
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
                            model: typeof tio !== "undefined" ? tio.flowOptions : []
                            value: typeof tio !== "undefined" ? tio.flow : ""
                            onPicked: function (choice) { tio.setFlow(choice) }
                        }
                    }
                }

                // ---- control lines ----
                GroupCard {
                    id: linesCard
                    title: window.tr("控制线 DTR / RTS", "Control lines DTR / RTS")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(linesCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(linesCard.title, folded) }
                    note: window.tr("阻塞：无法提供", "blocked: cannot be offered")
                    warn: true

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: DesignTokens.mutedColor
                        font.pixelSize: DesignTokens.fontCaption
                        text: window.tr(
                            "锁定的 tio 3.9 没有设置 DTR/RTS 的命令行选项（--help 已核对），所以这里没有可选的模式。"
                            + "打开端口、运行、正常停止、异常退出各阶段的电平变化都还没有实测，本窗口不声称默认行为是“保持”或“安全”，"
                            + "也不承诺打开端口不会产生控制线瞬态。",
                            "The pinned tio 3.9 has no command-line option for DTR/RTS (checked against --help), "
                            + "so there is no mode to choose. The levels at open, run, clean stop and abnormal exit have "
                            + "not been measured, so this window claims neither \"held\" nor \"safe\" for the default, and "
                            + "makes no promise that opening the port leaves the control lines free of a transient.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        CheckBox {
                            enabled: false
                            text: window.tr("DTR 保持（需要实测）", "DTR held (needs measurement)")
                        }
                        CheckBox {
                            enabled: false
                            text: window.tr("RTS 保持（需要实测）", "RTS held (needs measurement)")
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
                GroupCard {
                    id: packageCard
                    title: window.tr("包完整性", "Package integrity")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(packageCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(packageCard.title, folded) }
                    note: typeof tio !== "undefined" ? tio.packageVersion : ""
                    warn: !(typeof tio !== "undefined" && tio.packageValid)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: typeof tio !== "undefined" && tio.packageValid
                                  ? window.tr("有效 · 每个文件都与清单一致",
                                              "valid · every file matches the manifest")
                                  : window.tr("校验失败，不能启动", "the check failed: launch is refused")
                            color: typeof tio !== "undefined" && tio.packageValid
                                   ? DesignTokens.okColor : DesignTokens.dangerColor
                            font.pixelSize: DesignTokens.fontCaption
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: typeof tio !== "undefined" && !tio.packageValid
                            text: typeof tio !== "undefined" ? tio.packageProblem : ""
                            color: DesignTokens.mutedColor
                            font.pixelSize: DesignTokens.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                }

                // ---- session ----
                GroupCard {
                    id: sessionCard
                    title: window.tr("会话", "Session")
                    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(sessionCard.title)
                    onToggled: function (folded) { tio.setCardCollapsed(sessionCard.title, folded) }
                    note: typeof tio !== "undefined" && tio.sessionBusy
                          ? window.tr("正在启动或停止", "starting or stopping")
                          : typeof tio !== "undefined" && tio.sessionActive
                            ? window.tr("本插件启动的会话", "started by this plugin")
                            : window.tr("没有会话", "no session")

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 4
                        visible: typeof tio !== "undefined" && tio.sessionActive

                        Label { text: window.tr("阶段", "Phase"); color: DesignTokens.mutedColor; font.pixelSize: DesignTokens.fontSmall }
                        Label {
                            text: typeof tio !== "undefined" ? tio.sessionPhase : ""
                            color: DesignTokens.textColor; font.pixelSize: DesignTokens.fontCaption
                        }
                        Label { text: window.tr("端口状态", "Port state"); color: DesignTokens.mutedColor; font.pixelSize: DesignTokens.fontSmall }
                        Label {
                            text: typeof tio !== "undefined" ? tio.sessionPortState : ""
                            color: DesignTokens.warnColor; font.pixelSize: DesignTokens.fontCaption
                        }
                        Label { text: "PID"; color: DesignTokens.mutedColor; font.pixelSize: DesignTokens.fontSmall }
                        Label {
                            text: typeof tio !== "undefined" ? tio.sessionPid : ""
                            color: DesignTokens.textColor; font.pixelSize: DesignTokens.fontCaption
                        }
                        Label { text: window.tr("会话 ID", "Session id"); color: DesignTokens.mutedColor; font.pixelSize: DesignTokens.fontSmall }
                        Label {
                            text: typeof tio !== "undefined" ? tio.sessionToken : ""
                            color: DesignTokens.textColor; font.pixelSize: DesignTokens.fontCaption
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: DesignTokens.mutedColor
                        font.pixelSize: DesignTokens.fontSmall
                        text: window.tr(
                            "“已启动”不等于“端口已打开”：插件不读 tio 的终端输出，端口状态只有在上游给出可验证信号时才会变化。",
                            "\"Started\" is not \"port open\": the plugin does not read tio's terminal output, so the "
                            + "port state only changes when the upstream gives a verifiable signal.")
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: typeof tio !== "undefined" && tio.canLaunch
                              ? window.tr("就绪", "Ready")
                              : window.tr("启动被阻塞：", "launch blocked: ")
                                + (typeof tio !== "undefined" ? tio.blockedReason : "")
                        color: typeof tio !== "undefined" && tio.canLaunch
                               ? DesignTokens.mutedColor : DesignTokens.warnColor
                        font.pixelSize: DesignTokens.fontSmall
                        elide: Text.ElideRight
                    }

                    TokenButton {
                        text: window.tr("保存配置", "Save")
                        enabled: typeof tio !== "undefined" && tio.dirty
                        onClicked: tio.save()
                    }
                    TokenButton {
                        text: window.tr("启动", "Launch")
                        enabled: typeof tio !== "undefined" && tio.canLaunch && !tio.sessionActive
                        highlighted: true
                        onClicked: tio.launch()
                    }
                    TokenButton {
                        text: window.tr("停止", "Stop")
                        enabled: typeof tio !== "undefined" && tio.sessionActive
                        onClicked: tio.stop()
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
                          : window.tr("终端由 Windows Terminal 或 conhost 提供；插件不读取、不显示、不记录串口内容。",
                                      "Windows Terminal or conhost provides the terminal; the plugin never reads, renders or logs serial content.")

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
