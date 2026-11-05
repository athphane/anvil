import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1120; height: 740
    minimumWidth: 600; minimumHeight: 420
    visible: true
    title: "Anvil"
    color: "#101114"
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius
    component AnvilButton: Button {
        id: control
        implicitWidth: Math.max(70, contentItem.implicitWidth + 24)
        implicitHeight: 40
        background: Rectangle {
            radius: theme.radius
            color: control.highlighted ? theme.accent : control.down ? "#363b45" : control.hovered ? "#30343d" : "#23262d"
            opacity: control.enabled ? 1 : 0.45
            border.color: control.highlighted ? theme.accent : "#383d48"
        }
        contentItem: Text {
            text: control.text; font: control.font
            color: control.highlighted ? theme.accentForeground : control.enabled ? "#e7e9ee" : "#747b88"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
    }
    property var selected: connections.currentIndex >= 0 && connections.currentIndex < backend.entries.length ? backend.entries[connections.currentIndex] : ({})
    property bool editing: search.activeFocus || account.visible || options.visible
    property bool compact: width < 900 || height < 650
    function connectSelected() {
        if (selected.key && !backend.connecting) backend.connectEntry(selected, hostOverride.text, userOverride.text, Number(portOverride.text), identityOverride.text, installMissing.checked, publicKey.text)
    }
    function copySelected() {
        if (selected.key) backend.copyCommand(backend.command(selected, hostOverride.text, userOverride.text, Number(portOverride.text), identityOverride.text))
    }
    onSelectedChanged: { hostOverride.clear(); userOverride.clear(); portOverride.clear(); identityOverride.clear() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.editing; onActivated: help.open() }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.editing && !help.visible; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.editing && !help.visible; onActivated: win.connectSelected() }
    Shortcut { sequences: ["Return", "Enter"]; context: Qt.ApplicationShortcut; enabled: !win.editing && !help.visible; onActivated: win.connectSelected() }
    Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; onActivated: { search.forceActiveFocus(); search.selectAll() } }
    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; enabled: !account.visible; onActivated: backend.refresh() }
    Shortcut { sequence: "Ctrl+Shift+C"; context: Qt.ApplicationShortcut; enabled: !account.visible && !options.visible; onActivated: win.copySelected() }
    Shortcut { sequence: "Ctrl+,"; context: Qt.ApplicationShortcut; onActivated: account.open() }
    Connections {
        target: backend
        function onEntriesChanged() { connections.currentIndex = backend.entries.length > 0 ? 0 : -1 }
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        ColumnLayout {
            anchors.fill: parent; anchors.margins: win.compact ? 20 : 28; spacing: win.compact ? 12 : 20
            RowLayout {
                spacing: 14
                Rectangle {
                    width: 44; height: 44; radius: theme.radius; color: theme.accent
                    Label { anchors.centerIn: parent; text: ">_"; font.family: "monospace"; font.pixelSize: 23; font.bold: true; color: theme.accentForeground }
                }
                ColumnLayout {
                    spacing: 2
                    Label { text: "Anvil"; font.pixelSize: 24; font.bold: true; color: "#f1f1f4" }
                    Label { text: qsTr("Your Forge. One connection away."); color: "#92969f"; font.pixelSize: 13 }
                }
                Item { Layout.fillWidth: true }
                Label { visible: backend.demo; text: qsTr("DEMO"); color: theme.accent; font.pixelSize: 11; font.letterSpacing: 2 }
                BusyIndicator { running: backend.busy || backend.connecting; visible: running; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                AnvilButton { text: qsTr("Cancel"); visible: backend.connecting; onClicked: backend.cancelConnection() }
                AnvilButton { text: qsTr("Refresh"); visible: !backend.connecting; enabled: backend.hasToken && !backend.busy; onClicked: backend.refresh() }
                AnvilButton { text: qsTr("Account"); enabled: !backend.connecting; onClicked: account.open() }
                AnvilButton { text: "?"; implicitWidth: 40; onClicked: help.open(); Accessible.name: qsTr("Keyboard shortcuts") }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#2b2d34" }
            Rectangle {
                id: errorBanner
                objectName: "errorBanner"
                Layout.fillWidth: true
                implicitHeight: errorContent.implicitHeight + 28
                visible: backend.error.length > 0
                radius: theme.radius; color: "#2a2025"; border.color: "#60404a"
                property int splitAt: backend.error.indexOf("\n")
                RowLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 14
                    ColumnLayout {
                        id: errorContent
                        Layout.fillWidth: true; spacing: 6
                        Label {
                            objectName: "errorHeading"
                            text: errorBanner.splitAt >= 0 ? backend.error.slice(0, errorBanner.splitAt) : qsTr("Something needs attention")
                            color: "#f2c5ce"; font.bold: true; font.pixelSize: 15
                            wrapMode: Text.Wrap; textFormat: Text.PlainText; Layout.fillWidth: true
                        }
                        Label {
                            objectName: "errorBody"
                            text: errorBanner.splitAt >= 0 ? backend.error.slice(errorBanner.splitAt + 1) : backend.error
                            color: "#c9b4bd"; font.pixelSize: 13
                            wrapMode: Text.Wrap; textFormat: Text.PlainText; Layout.fillWidth: true
                        }
                    }
                    AnvilButton {
                        objectName: "errorDismiss"; text: "×"
                        implicitWidth: 32; implicitHeight: 32; padding: 0
                        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
                        topInset: 0; bottomInset: 0; leftInset: 0; rightInset: 0
                        Layout.alignment: Qt.AlignVCenter
                        contentItem: Item {
                            implicitWidth: 16; implicitHeight: 16
                            Text { anchors.centerIn: parent; text: "×"; font.pixelSize: 16; color: "#e7e9ee" }
                        }
                        Accessible.name: qsTr("Dismiss error")
                        onClicked: backend.clearError()
                    }
                }
            }
            RowLayout {
                visible: backend.hasToken || backend.total > 0
                Layout.fillWidth: true; spacing: 16
                TextField {
                    id: search; objectName: "search"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search servers, sites, organizations…")
                    text: backend.query
                    onTextEdited: backend.query = text
                    Keys.onDownPressed: { if (connections.count) { connections.forceActiveFocus(); connections.currentIndex = Math.min(connections.count - 1, connections.currentIndex + 1) } }
                    Keys.onEscapePressed: { clear(); backend.query = ""; connections.forceActiveFocus() }
                    onAccepted: { connections.forceActiveFocus(); win.connectSelected() }
                }
                TabBar {
                    id: filters; objectName: "filters"
                    Layout.preferredWidth: 300
                    currentIndex: backend.filter
                    onCurrentIndexChanged: backend.filter = currentIndex
                    TabButton { text: qsTr("All") }
                    TabButton { text: qsTr("Servers") }
                    TabButton { text: qsTr("Sites") }
                }
            }
            RowLayout {
                visible: backend.hasToken || backend.total > 0
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 20
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    color: "#17191e"; radius: theme.radius; border.color: "#2b2d34"
                    ListView {
                        id: connections; objectName: "connections"
                        anchors.fill: parent; anchors.margins: 8
                        clip: true; spacing: 4; focus: true
                        model: backend.entries
                        currentIndex: count > 0 ? 0 : -1
                        keyNavigationWraps: true
                        ScrollBar.vertical: ScrollBar { }
                        delegate: ItemDelegate {
                            id: row
                            required property var modelData
                            required property int index
                            width: ListView.view.width; height: 84
                            highlighted: connections.currentIndex === index
                            onClicked: { connections.currentIndex = index; connections.forceActiveFocus() }
                            onDoubleClicked: win.connectSelected()
                            background: Rectangle {
                                radius: theme.radius
                                color: row.highlighted ? "#292c33" : row.hovered ? "#1f2228" : "transparent"
                                border.color: row.highlighted ? theme.accent : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: 14
                                Label { text: row.modelData.kind === "server" ? ">_" : "~/"; font.family: "monospace"; font.pixelSize: 18; color: row.highlighted ? theme.accent : "#737987"; Layout.preferredWidth: 32 }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 6
                                    Label { text: row.modelData.name; textFormat: Text.PlainText; font.pixelSize: 15; font.bold: true; color: "#eff0f3"; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: qsTr("%1 / %2").arg(row.modelData.org).arg(row.modelData.kind === "site" ? row.modelData.server : row.modelData.host || qsTr("No IP address")); textFormat: Text.PlainText; color: "#9298a5"; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                Label { text: row.modelData.kind === "server" ? qsTr("SERVER") : qsTr("SITE"); font.pixelSize: 10; font.letterSpacing: 1; color: "#9b9fac"; Layout.rightMargin: 8 }
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent; width: parent.width - 48
                        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                        visible: connections.count === 0
                        text: backend.busy ? qsTr("Finding your connections…") : backend.query.length ? qsTr("No matching connections.") : qsTr("No connections yet. Refresh or check your token permissions.")
                        color: "#9298a5"
                    }
                }
                Rectangle {
                    visible: !win.compact
                    Layout.preferredWidth: 340; Layout.fillHeight: true
                    color: "#17191e"; radius: theme.radius; border.color: "#2b2d34"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 20; spacing: 10
                        Label { text: qsTr("CONNECTION"); color: theme.accent; font.pixelSize: 11; font.letterSpacing: 2 }
                        Label { text: win.selected.name || qsTr("Select a connection"); textFormat: Text.PlainText; font.pixelSize: 22; font.bold: true; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                        Label { text: win.selected.org || ""; textFormat: Text.PlainText; color: "#9298a5"; Layout.fillWidth: true; elide: Text.ElideRight }
                        Rectangle { height: 1; color: "#30323a"; Layout.fillWidth: true }
                        Repeater {
                            model: [
                                {label: qsTr("Host"), value: hostOverride.text || win.selected.host || "—"},
                                {label: qsTr("User"), value: userOverride.text || win.selected.user || "—"},
                                {label: qsTr("Port"), value: portOverride.text || String(win.selected.port || "—")},
                                {label: qsTr("Directory"), value: win.selected.path || qsTr("Home directory")}
                            ]
                            RowLayout {
                                required property var modelData
                                spacing: 10; Layout.fillWidth: true
                                Label { text: modelData.label; color: "#818897"; font.pixelSize: 12; Layout.preferredWidth: 70; Layout.alignment: Qt.AlignTop }
                                Label { text: modelData.value; textFormat: Text.PlainText; font.family: "monospace"; font.pixelSize: 13; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                            }
                        }
                        Label { visible: win.selected.ready === false; text: qsTr("Forge reports this server is not ready or access is revoked."); wrapMode: Text.Wrap; color: "#efbc84"; Layout.fillWidth: true }
                        Item { Layout.fillHeight: true }
                        Label { text: qsTr("Uses your SSH keys, agent, and config."); color: "#9298a5"; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        AnvilButton { text: qsTr("Connection options"); Layout.fillWidth: true; enabled: !!win.selected.key; onClicked: options.open() }
                        RowLayout {
                            Layout.fillWidth: true
                            AnvilButton { text: qsTr("Copy SSH"); enabled: !!win.selected.key; onClicked: win.copySelected() }
                            AnvilButton { objectName: "connectButton"; text: qsTr("Connect"); highlighted: true; Layout.fillWidth: true; enabled: !!win.selected.key && !!win.selected.host && !backend.connecting; onClicked: win.connectSelected() }
                        }
                    }
                }
            }
            RowLayout {
                visible: win.compact && (backend.hasToken || backend.total > 0)
                Layout.fillWidth: true; spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 3
                    Label { text: win.selected.name || qsTr("Select a connection"); textFormat: Text.PlainText; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: win.selected.host ? qsTr("%1@%2").arg(userOverride.text || win.selected.user).arg(hostOverride.text || win.selected.host) : ""; textFormat: Text.PlainText; color: "#9298a5"; elide: Text.ElideRight; font.pixelSize: 12; Layout.fillWidth: true }
                }
                AnvilButton { text: qsTr("Options"); enabled: !!win.selected.key; onClicked: options.open() }
                AnvilButton { text: qsTr("Copy SSH"); enabled: !!win.selected.key; onClicked: win.copySelected() }
                AnvilButton { objectName: "compactConnect"; text: qsTr("Connect"); highlighted: true; enabled: !!win.selected.key && !!win.selected.host && !backend.connecting; onClicked: win.connectSelected() }
            }
            Item {
                visible: !backend.hasToken && backend.total === 0
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.centerIn: parent; width: Math.min(480, parent.width); spacing: win.compact ? 12 : 20
                    Label { visible: !win.compact; text: ">_"; font.family: "monospace"; color: theme.accent; font.pixelSize: 54; Layout.alignment: Qt.AlignHCenter }
                    Label { text: qsTr("Your servers, without the setup."); font.pixelSize: win.compact ? 22 : 27; font.bold: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: win.compact ? qsTr("Browse your Forge servers and sites, then connect with SSH.") : qsTr("Connect your Laravel Forge account to browse every server and site you can access, then jump straight into SSH."); horizontalAlignment: Text.AlignHCenter; color: "#a3a8b3"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 15 }
                    AnvilButton { objectName: "addAccount"; text: qsTr("Connect Laravel Forge"); highlighted: true; Layout.alignment: Qt.AlignHCenter; onClicked: account.open() }
                    Label { text: qsTr("Adds missing public keys when you connect."); color: "#777f8e"; Layout.alignment: Qt.AlignHCenter }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: backend.busy ? qsTr("Syncing with Forge…") : backend.message || (translator.language, qsTr("%n connections", "", backend.total)); color: "#979daa"; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: win.compact ? qsTr("Enter  Connect    ?  Shortcuts") : qsTr("Ctrl+K  Search    Enter  Connect    ?  Shortcuts"); color: "#717988"; font.pixelSize: 11 }
            }
        }
    }
    Dialog {
        id: account; objectName: "account"
        anchors.centerIn: parent; width: 580; modal: true
        background: Rectangle { color: "#20232a"; radius: theme.radius; border.color: "#444b59" }
        Overlay.modal: Rectangle { color: "#b8000000" }
        title: qsTr("Laravel Forge account")
        onClosed: token.clear()
        contentItem: ColumnLayout {
            LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
            LayoutMirroring.childrenInherit: true
            spacing: 16
            Label { text: qsTr("Required token scopes: organization:view and server:view."); wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("Automatic SSH key installation also needs server:create-keys."); wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#9ca2af" }
            TextField { id: token; objectName: "token"; placeholderText: qsTr("Forge API token"); echoMode: TextInput.Password; selectByMouse: true; Layout.fillWidth: true }
            CheckBox { id: remember; text: qsTr("Remember in the desktop keyring"); checked: true }
            Label { text: qsTr("SSH still uses the keys already on this computer. Your token is never stored in a text file."); color: "#9ca2af"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                AnvilButton { text: qsTr("Create token"); onClicked: backend.openTokenPage() }
                AnvilButton { text: qsTr("Forget token"); enabled: backend.hasToken; onClicked: { backend.forgetToken(); account.close() } }
                Item { Layout.fillWidth: true }
                AnvilButton { text: qsTr("Cancel"); onClicked: account.close() }
                AnvilButton { text: qsTr("Connect"); highlighted: true; enabled: token.text.trim().length > 0; onClicked: { backend.authenticate(token.text, remember.checked); account.close() } }
            }
        }
    }
    Dialog {
        id: options
        objectName: "options"
        anchors.centerIn: parent; width: 520; height: Math.min(implicitHeight, win.height - 32); modal: true
        background: Rectangle { color: "#20232a"; radius: theme.radius; border.color: "#444b59" }
        Overlay.modal: Rectangle { color: "#b8000000" }
        title: qsTr("Connection options")
        footer: DialogButtonBox { background: Rectangle { color: "#20232a"; radius: theme.radius } AnvilButton { text: qsTr("Done"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: options.close() } }
        contentItem: ScrollView {
            id: optionsScroll
            clip: true
            implicitHeight: optionsFields.implicitHeight
            ColumnLayout {
            id: optionsFields
            width: optionsScroll.availableWidth
            LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
            LayoutMirroring.childrenInherit: true
            spacing: 12
            CheckBox { id: installMissing; objectName: "installMissing"; text: qsTr("Add missing SSH keys through Forge"); checked: true }
            TextField { id: publicKey; objectName: "publicKey"; text: backend.suggestedPublicKey; placeholderText: qsTr("Public key file (.pub)"); enabled: installMissing.checked; Layout.fillWidth: true }
            Label { text: qsTr("Connect checks SSH access first. If needed, it adds this public key for the selected server user and waits for Forge. An explicit identity uses its matching .pub file."); color: "#9ca2af"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("Optional overrides for this selection. Leave blank to use Forge and your OpenSSH configuration."); wrapMode: Text.Wrap; Layout.fillWidth: true }
            TextField { id: hostOverride; placeholderText: qsTr("Hostname or SSH config alias"); Layout.fillWidth: true }
            TextField { id: userOverride; placeholderText: qsTr("SSH user"); Layout.fillWidth: true }
            TextField { id: portOverride; placeholderText: qsTr("SSH port"); validator: IntValidator { bottom: 1; top: 65535 } Layout.fillWidth: true }
            TextField { id: identityOverride; placeholderText: qsTr("Identity file path (optional)"); Layout.fillWidth: true }
            }
        }
    }
    Dialog {
        id: help; objectName: "help"
        anchors.centerIn: parent; width: 520; modal: true
        background: Rectangle { color: "#20232a"; radius: theme.radius; border.color: "#444b59" }
        Overlay.modal: Rectangle { color: "#b8000000" }
        title: qsTr("Keyboard shortcuts")
        footer: DialogButtonBox { AnvilButton { text: qsTr("Close"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: help.close() } }
        contentItem: ColumnLayout {
            LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
            LayoutMirroring.childrenInherit: true
            spacing: 15
            Repeater {
                model: [
                    {key: "Ctrl+K", action: qsTr("Search connections")},
                    {key: "↑ / ↓", action: qsTr("Select a connection")},
                    {key: "Enter / Space", action: qsTr("Open SSH in your terminal")},
                    {key: "Ctrl+Shift+C", action: qsTr("Copy the SSH command")},
                    {key: "Ctrl+R", action: qsTr("Refresh from Forge")},
                    {key: "Ctrl+,", action: qsTr("Manage API token")},
                    {key: "Esc", action: qsTr("Close a dialog or clear search")},
                    {key: "?", action: qsTr("Show shortcuts")},
                    {key: "Q", action: qsTr("Quit")}
                ]
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: modelData.key; font.family: "monospace"; color: theme.accent; Layout.preferredWidth: 175 }
                    Label { text: modelData.action; Layout.fillWidth: true; wrapMode: Text.Wrap }
                }
            }
        }
    }
}
