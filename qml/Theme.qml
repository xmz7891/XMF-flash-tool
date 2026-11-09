pragma Singleton
import QtQuick

// XMF desktop theme.
// A dense, flat desktop-utility look (not a marketing dashboard): white panels,
// 1px separators, small radii, YaHei UI at 12px. Light and dark only.
QtObject {
    id: theme

    property bool darkMode: false

    // ---- motion (deliberately short; desktop tools should feel snappy) ----
    property bool animations: true
    readonly property int durFast: animations ? 90 : 0
    readonly property int durNormal: animations ? 140 : 0
    readonly property int durSlow: animations ? 200 : 0
    readonly property int easing: Easing.OutCubic
    readonly property int spring: Easing.OutCubic

    // ---- window / surfaces ------------------------------------------------
    readonly property color windowBg: darkMode ? "#1C1E21" : "#F0F0F0"
    readonly property color windowTint: darkMode ? "#FF1C1E21" : "#FFF0F0F0"
    readonly property color windowBorder: darkMode ? "#44474B" : "#C8C8C8"

    readonly property color panel: darkMode ? "#2A2D31" : "#FFFFFF"
    readonly property color panelAlt: darkMode ? "#33373B" : "#F7F7F7"
    readonly property color panelHover: darkMode ? "#383C41" : "#F2F6FB"
    readonly property color toolbar: darkMode ? "#2D3034" : "#F5F5F5"
    readonly property color sidebar: darkMode ? "#24262A" : "#F4F4F4"

    readonly property color border: darkMode ? "#45494E" : "#D6D6D6"
    readonly property color borderStrong: darkMode ? "#54585D" : "#BFBFBF"
    readonly property color divider: darkMode ? "#3A3E43" : "#E3E3E3"

    // ---- text --------------------------------------------------------------
    readonly property color text: darkMode ? "#E9E9EA" : "#1A1A1A"
    readonly property color textSub: darkMode ? "#B4B4B8" : "#4F4F4F"
    readonly property color textMuted: darkMode ? "#88888C" : "#8A8A8A"
    readonly property color textOnAccent: "#FFFFFF"

    // ---- accent (classic desktop blue; primary actions only) --------------
    readonly property color accent: darkMode ? "#4CC2FF" : "#0067C0"
    readonly property color accentHover: darkMode ? "#6FCFFF" : "#005A9E"
    readonly property color accentSoft: darkMode ? "#173D53" : "#E5F1FB"
    readonly property color selection: darkMode ? "#204A66" : "#CCE4F7"

    // ---- status ------------------------------------------------------------
    readonly property color success: darkMode ? "#6CCB5F" : "#0F7B0F"
    readonly property color warning: darkMode ? "#FCE100" : "#9D5D00"
    readonly property color danger: darkMode ? "#FF99A4" : "#C42B1C"

    // ---- legacy aliases used by older components ---------------------------
    readonly property color accent2: darkMode ? "#3A3A3C" : "#E5E5EA"
    readonly property color bg0: windowBg
    readonly property color bg1: panel
    readonly property color glass: panel
    readonly property color glassHover: panelHover
    readonly property color glassStrong: panelAlt
    readonly property color glassHighlight: darkMode ? "#14FFFFFF" : "#FAFAFA"
    readonly property color borderAccent: accent
    readonly property color qcom: darkMode ? "#D9D9DE" : "#3A3A3C"
    readonly property color mtk: darkMode ? "#C2C2C8" : "#4A4A4C"
    readonly property color sprd: darkMode ? "#ABABB2" : "#5A5A5C"
    readonly property color rk: darkMode ? "#D9D9DE" : "#3A3A3C"
    readonly property color aw: darkMode ? "#C2C2C8" : "#4A4A4C"

    // ---- geometry ----------------------------------------------------------
    readonly property int r22: 6
    readonly property int r20: 6
    readonly property int r18: 5
    readonly property int r16: 4
    readonly property int r14: 4
    readonly property int r12: 4
    readonly property int r10: 3
    readonly property int r6: 2
    readonly property int pill: 4
    readonly property int pagePadding: 14

    readonly property string fontFamily: "Microsoft YaHei UI"

    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a);
    }

    function lighten(c, amount) {
        return Qt.rgba(Math.min(1, c.r + amount), Math.min(1, c.g + amount), Math.min(1, c.b + amount), c.a);
    }

    function platformColor(id) {
        switch (id) {
        case "qcom": return qcom
        case "mtk": return mtk
        case "sprd": return sprd
        case "rk": return rk
        case "aw": return aw
        }
        return textSub
    }

    function fadeGradient(c) {
        return [ { position: 0.0, color: alpha(c, 0.10) }, { position: 1.0, color: alpha(c, 0.0) } ];
    }
}
