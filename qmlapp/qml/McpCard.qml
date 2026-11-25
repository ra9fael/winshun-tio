// McpCard.qml - paste-ready MCP configuration for common AI clients.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

GroupCard {
    id: card

    property bool zh: true
    function tr(zhText, enText) { return card.zh ? zhText : enText }

    title: tr("MCP 使用", "MCP use")
    collapsed: typeof tio !== "undefined" && tio.cardCollapsed(card.title)
    onToggled: function (folded) {
        if (typeof tio !== "undefined")
            tio.setCardCollapsed(card.title, folded)
    }
    note: tioConfig.length > 0
          ? tr("复制 JSON 到 AI 客户端", "copy JSON into an AI client")
          : tr("需要安装版 Win顺", "requires an installed WinShun")

    readonly property var agents: [
        { id: "claude-desktop", title: "Claude Desktop" },
        { id: "claude-code", title: "Claude Code" },
        { id: "cursor", title: "Cursor" },
        { id: "vscode", title: "VS Code" }
    ]
    readonly property string tioConfig: typeof tio !== "undefined" ? tio.mcpConfig : ""

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: DesignTokens.mutedColor
        font.pixelSize: DesignTokens.fontSmall
        text: card.tr(
            "选择 AI 客户端，复制下面的 JSON 到它的 MCP 配置中。配置只连接 Win顺 MCP 中继；要读写串口，还需要先在本窗口启动并共享 tio 会话，并在 Win顺 设置中允许 AI 使用插件。",
            "Choose an AI client and copy the JSON into its MCP configuration. The entry connects only to WinShun's MCP relay; serial I/O also requires a shared tio session and permission for AI to use plugins in WinShun settings.")
    }

    FieldRow {
        label: card.tr("客户端", "Client")
        hint: card.tr("常用客户端的配置格式", "common client configuration")
        TokenComboBox {
            id: agentBox
            Layout.fillWidth: true
            model: card.agents
            currentIndex: {
                if (typeof tio === "undefined")
                    return 0
                for (let i = 0; i < card.agents.length; ++i)
                    if (card.agents[i].id === tio.mcpAgent)
                        return i
                return 0
            }
            textRole: "title"
            onActivated: function (index) {
                if (typeof tio !== "undefined" && index >= 0)
                    tio.mcpAgent = card.agents[index].id
            }
            Accessible.name: card.tr("MCP 客户端", "MCP client")
        }
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: DesignTokens.faintColor
        font.pixelSize: DesignTokens.fontSmall
        text: card.tr("配置中的 command 使用 WinShunMcp.exe 的绝对路径，不会写入或修改客户端配置。",
                      "The command uses the absolute path to WinShunMcp.exe. This card does not write or modify the client configuration.")
    }

    TextArea {
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        readOnly: true
        selectByMouse: true
        textFormat: TextEdit.PlainText
        wrapMode: TextArea.NoWrap
        font.family: "Consolas"
        font.pixelSize: DesignTokens.fontSmall
        color: DesignTokens.textColor
        background: Rectangle {
            radius: DesignTokens.controlRadius
            color: DesignTokens.sunkenColor
            border.width: 1
            border.color: DesignTokens.borderColor
        }
        text: card.tioConfig.length > 0 ? card.tioConfig : card.tr("未找到 WinShunMcp.exe", "WinShunMcp.exe was not found")
    }

    RowLayout {
        Layout.fillWidth: true
        TokenButton {
            Layout.alignment: Qt.AlignRight
            text: card.tr("复制 JSON", "Copy JSON")
            highlighted: true
            enabled: card.tioConfig.length > 0
            onClicked: if (typeof tio !== "undefined") tio.copyMcpConfig()
        }
    }
}
